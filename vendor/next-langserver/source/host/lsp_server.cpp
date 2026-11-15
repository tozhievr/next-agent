#include "host/lsp_server.hpp"

#include "host/services.hpp"
#include "host/uri.hpp"

#include <algorithm>
#include <filesystem>

namespace lx::host {

namespace {

JsonValue doc_symbol_to_json(const lx::Rope& rope,
                             const DocumentSymbolNode& ds,
                             const std::function<JsonValue(const lx::Rope&, lx_range)>& mk_range) {
  JsonObject obj;
  obj["name"] = ds.name;
  if (!ds.detail.empty()) obj["detail"] = ds.detail;
  obj["kind"] = ds.kind;
  obj["range"] = mk_range(rope, lx_range{ds.range.start, ds.range.end});
  obj["selectionRange"] = mk_range(rope, lx_range{ds.selection_range.start, ds.selection_range.end});
  if (!ds.children.empty()) {
    JsonArray arr;
    arr.reserve(ds.children.size());
    for (const auto& ch : ds.children) {
      arr.push_back(doc_symbol_to_json(rope, ch, mk_range));
    }
    obj["children"] = JsonValue{std::move(arr)};
  }
  return JsonValue{std::move(obj)};
}

}  // namespace

LspServer::LspServer(HostConfig config, ParseBackendFn parse_fn)
    : config_(config),
      registry_(config_, &metrics_, &trace_),
      scheduler_(config_.scheduler_workers, config_.debounce),
      store_(registry_, scheduler_, std::move(parse_fn), config_, &metrics_, &trace_) {}

LspServer::~LspServer() {
  store_.set_publish_callback(nullptr);
  scheduler_.shutdown();
}

JsonValue LspServer::make_lsp_position(const lx::Rope& rope, uint32_t byte_off) const {
  if (byte_off > rope.byte_length()) byte_off = rope.byte_length();
  lx::Point pt = rope.byte_to_point(byte_off);
  uint32_t line_start_b = rope.line_start_byte(pt.line);
  uint32_t u16_col = rope.byte_to_utf16(byte_off) - rope.byte_to_utf16(line_start_b);
  JsonObject pos;
  pos["line"] = static_cast<int64_t>(pt.line);
  pos["character"] = static_cast<int64_t>(u16_col);
  return JsonValue{std::move(pos)};
}

JsonValue LspServer::make_lsp_range(const lx::Rope& rope, lx_range r) const {
  JsonObject range;
  range["start"] = make_lsp_position(rope, r.start);
  range["end"] = make_lsp_position(rope, r.end);
  return JsonValue{std::move(range)};
}

uint32_t LspServer::lsp_pos_to_byte(const lx::Rope& rope, const JsonValue& pos_obj) const {
  uint32_t line = static_cast<uint32_t>(std::max<int64_t>(0, pos_obj.at("line").as_int(0)));
  uint32_t ch = static_cast<uint32_t>(std::max<int64_t>(0, pos_obj.at("character").as_int(0)));
  if (line >= rope.line_count()) return rope.byte_length();
  uint32_t ls = rope.line_start_byte(line);
  uint32_t le = rope.line_end_byte(line);
  uint32_t ls_u16 = rope.byte_to_utf16(ls);
  uint32_t le_u16 = rope.byte_to_utf16(le);
  uint32_t target = std::min(ls_u16 + ch, le_u16);
  return rope.utf16_to_byte(target);
}

void LspServer::publish_diagnostics(const std::string& uri,
                                    const std::shared_ptr<const Snapshot>& snap,
                                    const std::vector<Diagnostic>& diags,
                                    int64_t lsp_version,
                                    LspTransport& tr) {
  if (!snap) return;
  JsonArray arr;
  arr.reserve(diags.size());
  for (const auto& d : diags) {
    JsonObject item;
    item["range"] = make_lsp_range(snap->rope, d.range);
    item["severity"] = static_cast<int>(d.severity);
    item["code"] = d.code;
    item["source"] = "lxserver";
    item["message"] = d.message;
    arr.push_back(JsonValue{std::move(item)});
  }
  JsonObject params;
  params["uri"] = uri;
  params["version"] = lsp_version;
  params["diagnostics"] = JsonValue{std::move(arr)};
  tr.send_notification("textDocument/publishDiagnostics", JsonValue{std::move(params)});
}

int LspServer::run(std::istream& in, std::ostream& out) {
  LspTransport transport(in, out);
  active_transport_ = &transport;

  store_.set_publish_callback(
      [this, &transport](const std::string& uri,
                         const std::shared_ptr<const Snapshot>& snap,
                         const std::vector<Diagnostic>& diags,
                         int64_t ver) {
        publish_diagnostics(uri, snap, diags, ver, transport);
      });

  while (!exit_requested_.load(std::memory_order_acquire)) {
    std::string err;
    auto msg = transport.read_message(&err);
    if (!msg) break;
    handle_message(*msg, transport);
  }

  store_.set_publish_callback(nullptr);
  active_transport_ = nullptr;
  return shutdown_requested_.load(std::memory_order_acquire) ? 0 : 1;
}

void LspServer::handle_message(const JsonValue& msg, LspTransport& tr) {
  if (!msg.is_object()) return;
  const JsonValue* id = msg.find("id");
  std::string_view method = msg.at("method").as_string();
  const JsonValue& params = msg.at("params");

  if (method == "initialize") {
    if (id) handle_initialize(*id, params, tr);
    return;
  }
  if (method == "initialized") {
    return;
  }
  if (method == "shutdown") {
    shutdown_requested_.store(true, std::memory_order_release);
    if (id) tr.send_response(*id, JsonValue{nullptr});
    return;
  }
  if (method == "exit") {
    exit_requested_.store(true, std::memory_order_release);
    return;
  }
  if (shutdown_requested_.load(std::memory_order_acquire)) {
    if (id) tr.send_error(*id, -32600, "server is shutting down");
    return;
  }

  if (method == "textDocument/didOpen") {
    handle_did_open(params);
  } else if (method == "textDocument/didChange") {
    handle_did_change(params);
  } else if (method == "textDocument/didClose") {
    handle_did_close(params, tr);
  } else if (method == "textDocument/semanticTokens/full") {
    if (id) handle_semantic_tokens_full(*id, params, tr);
  } else if (method == "textDocument/semanticTokens/range") {
    if (id) handle_semantic_tokens_range(*id, params, tr);
  } else if (method == "textDocument/foldingRange") {
    if (id) handle_folding_range(*id, params, tr);
  } else if (method == "textDocument/documentSymbol") {
    if (id) handle_document_symbol(*id, params, tr);
  } else if (method == "textDocument/selectionRange") {
    if (id) handle_selection_range(*id, params, tr);
  } else if (method == "lx/reloadLanguage") {
    std::string lang(params.at("languageId").as_string());
    std::string path(params.at("path").as_string());
    std::string err;
    auto mod = registry_.reload_language(lang, path, &err);
    if (mod) {
      store_.notify_language_reloaded(lang);
      if (id) {
        JsonObject res;
        res["ok"] = true;
        res["epoch"] = static_cast<int64_t>(mod->module_epoch);
        tr.send_response(*id, JsonValue{std::move(res)});
      }
    } else if (id) {
      tr.send_error(*id, -32000, err.empty() ? "reload failed" : err);
    }
  } else if (method == "lx/metrics") {
    if (id) {
      JsonObject res;
      res["reparses_total"] = static_cast<int64_t>(metrics_.reparses_total.load());
      res["reparses_full"] = static_cast<int64_t>(metrics_.reparses_full.load());
      res["reparses_incremental"] = static_cast<int64_t>(metrics_.reparses_incremental.load());
      res["reparses_cancelled"] = static_cast<int64_t>(metrics_.reparses_cancelled.load());
      res["tokens_relexed"] = static_cast<int64_t>(metrics_.tokens_relexed.load());
      res["nodes_reused"] = static_cast<int64_t>(metrics_.nodes_reused.load());
      res["nodes_created"] = static_cast<int64_t>(metrics_.nodes_created.load());
      res["reuse_ratio"] = metrics_.average_reuse_ratio();
      res["module_crashes"] = static_cast<int64_t>(metrics_.module_crashes.load());
      tr.send_response(*id, JsonValue{std::move(res)});
    }
  } else if (id) {
    tr.send_error(*id, -32601, "method not found: " + std::string(method));
  }
}

void LspServer::handle_initialize(const JsonValue& id,
                                  const JsonValue& params,
                                  LspTransport& tr) {
  // Index any plugin directories supplied in initializationOptions or workspace root
  if (const auto* init_opts = params.find("initializationOptions");
      init_opts && init_opts->is_object()) {
    for (const auto& dir_v : init_opts->at("pluginDirs").as_array()) {
      if (dir_v.is_string() && !dir_v.as_string().empty()) {
        registry_.index_directory(std::filesystem::path(dir_v.as_string()));
      }
    }
    for (const auto& m_v : init_opts->at("manifests").as_array()) {
      if (m_v.is_string() && !m_v.as_string().empty()) {
        registry_.register_manifest(std::filesystem::path(m_v.as_string()));
      }
    }
  }

  if (const auto* root_uri = params.find("rootUri"); root_uri && root_uri->is_string()) {
    std::string local = uri_to_path(root_uri->as_string());
    if (!local.empty()) {
      auto plugins_dir = std::filesystem::path(local) / "plugins";
      registry_.index_directory(plugins_dir);
    }
  }

  JsonArray token_types;
  for (const char* t : kSemanticTokenLegendTypes) token_types.emplace_back(t);
  JsonArray token_mods;
  for (const char* m : kSemanticTokenLegendModifiers) token_mods.emplace_back(m);

  JsonObject legend;
  legend["tokenTypes"] = JsonValue{std::move(token_types)};
  legend["tokenModifiers"] = JsonValue{std::move(token_mods)};

  JsonObject sem_tok;
  sem_tok["legend"] = JsonValue{std::move(legend)};
  sem_tok["full"] = true;
  sem_tok["range"] = true;

  JsonObject sync_opts;
  sync_opts["openClose"] = true;
  sync_opts["change"] = 2;  // TextDocumentSyncKind.Incremental

  JsonObject caps;
  caps["textDocumentSync"] = JsonValue{std::move(sync_opts)};
  caps["semanticTokensProvider"] = JsonValue{std::move(sem_tok)};
  caps["foldingRangeProvider"] = true;
  caps["documentSymbolProvider"] = true;
  caps["selectionRangeProvider"] = true;

  JsonObject server_info;
  server_info["name"] = "lxserver";
  server_info["version"] = "0.1.0";

  JsonObject res;
  res["capabilities"] = JsonValue{std::move(caps)};
  res["serverInfo"] = JsonValue{std::move(server_info)};
  tr.send_response(id, JsonValue{std::move(res)});
}

void LspServer::handle_did_open(const JsonValue& params) {
  const auto& td = params.at("textDocument");
  std::string uri(td.at("uri").as_string());
  std::string lang(td.at("languageId").as_string());
  std::string text(td.at("text").as_string());
  int64_t ver = td.at("version").as_int(0);
  if (uri.empty()) return;

  std::string err;
  auto sess = store_.open_document(uri, text, lang, ver, /*sync_parse=*/true, &err);
  if (sess && active_transport_) {
    auto snap = sess->snapshot();
    if (snap) {
      auto diags = sess->layered_cursor().all_diagnostics();
      publish_diagnostics(uri, snap, diags, ver, *active_transport_);
    }
  } else if (!sess && !err.empty()) {
    trace_.log(LogLevel::Warn, "lsp", uri, "didOpen failed: " + err);
  }
}

void LspServer::handle_did_change(const JsonValue& params) {
  const auto& td = params.at("textDocument");
  std::string uri(td.at("uri").as_string());
  int64_t ver = td.at("version").as_int(0);
  if (uri.empty()) return;

  std::vector<TextEdit> edits;
  for (const auto& ch : params.at("contentChanges").as_array()) {
    TextEdit te;
    te.text = std::string(ch.at("text").as_string());
    if (const auto* r = ch.find("range"); r && r->is_object()) {
      te.is_full_replace = false;
      const auto& s = r->at("start");
      const auto& e = r->at("end");
      te.range.start.line = static_cast<uint32_t>(std::max<int64_t>(0, s.at("line").as_int(0)));
      te.range.start.character =
          static_cast<uint32_t>(std::max<int64_t>(0, s.at("character").as_int(0)));
      te.range.end.line = static_cast<uint32_t>(std::max<int64_t>(0, e.at("line").as_int(0)));
      te.range.end.character =
          static_cast<uint32_t>(std::max<int64_t>(0, e.at("character").as_int(0)));
    } else {
      te.is_full_replace = true;
    }
    edits.push_back(std::move(te));
  }

  store_.apply_edits(uri, edits, ver, TaskPriority::Interactive);
}

void LspServer::handle_did_close(const JsonValue& params, LspTransport& tr) {
  std::string uri(params.at("textDocument").at("uri").as_string());
  if (uri.empty()) return;
  store_.close_document(uri);

  JsonObject clear_params;
  clear_params["uri"] = uri;
  clear_params["diagnostics"] = JsonValue{JsonArray{}};
  tr.send_notification("textDocument/publishDiagnostics", JsonValue{std::move(clear_params)});
}

void LspServer::handle_semantic_tokens_full(const JsonValue& id,
                                            const JsonValue& params,
                                            LspTransport& tr) {
  std::string uri(params.at("textDocument").at("uri").as_string());
  auto snap = store_.ensure_parsed(uri);
  auto sess = store_.get_session(uri);
  if (!snap || !sess) {
    JsonObject res;
    res["data"] = JsonValue{JsonArray{}};
    tr.send_response(id, JsonValue{std::move(res)});
    return;
  }

  auto spans = sess->layered_cursor().all_semantic_tokens();
  auto encoded = Services::encode_lsp_semantic_tokens(snap->rope, spans);
  JsonArray arr;
  arr.reserve(encoded.size());
  for (uint32_t v : encoded) arr.emplace_back(static_cast<int64_t>(v));

  JsonObject res;
  res["data"] = JsonValue{std::move(arr)};
  tr.send_response(id, JsonValue{std::move(res)});
}

void LspServer::handle_semantic_tokens_range(const JsonValue& id,
                                             const JsonValue& params,
                                             LspTransport& tr) {
  std::string uri(params.at("textDocument").at("uri").as_string());
  auto snap = store_.ensure_parsed(uri);
  auto sess = store_.get_session(uri);
  if (!snap || !sess) {
    JsonObject res;
    res["data"] = JsonValue{JsonArray{}};
    tr.send_response(id, JsonValue{std::move(res)});
    return;
  }

  const auto& r = params.at("range");
  uint32_t s_b = lsp_pos_to_byte(snap->rope, r.at("start"));
  uint32_t e_b = lsp_pos_to_byte(snap->rope, r.at("end"));
  if (e_b < s_b) e_b = s_b;

  auto spans = sess->layered_cursor().all_semantic_tokens(lx::uast::Range{s_b, e_b});
  auto encoded = Services::encode_lsp_semantic_tokens(snap->rope, spans);
  JsonArray arr;
  arr.reserve(encoded.size());
  for (uint32_t v : encoded) arr.emplace_back(static_cast<int64_t>(v));

  JsonObject res;
  res["data"] = JsonValue{std::move(arr)};
  tr.send_response(id, JsonValue{std::move(res)});
}

void LspServer::handle_folding_range(const JsonValue& id,
                                     const JsonValue& params,
                                     LspTransport& tr) {
  std::string uri(params.at("textDocument").at("uri").as_string());
  auto snap = store_.ensure_parsed(uri);
  if (!snap) {
    tr.send_response(id, JsonValue{JsonArray{}});
    return;
  }
  auto folds = Services::fold(*snap);
  JsonArray arr;
  arr.reserve(folds.size());
  for (const auto& f : folds) {
    JsonObject item;
    item["startLine"] = static_cast<int64_t>(f.start_line);
    item["endLine"] = static_cast<int64_t>(f.end_line);
    item["kind"] = f.kind;
    arr.push_back(JsonValue{std::move(item)});
  }
  tr.send_response(id, JsonValue{std::move(arr)});
}

void LspServer::handle_document_symbol(const JsonValue& id,
                                       const JsonValue& params,
                                       LspTransport& tr) {
  std::string uri(params.at("textDocument").at("uri").as_string());
  auto snap = store_.ensure_parsed(uri);
  if (!snap) {
    tr.send_response(id, JsonValue{JsonArray{}});
    return;
  }
  auto syms = Services::outline(*snap);
  auto mk = [this](const lx::Rope& rope, lx_range r) { return make_lsp_range(rope, r); };
  JsonArray arr;
  arr.reserve(syms.size());
  for (const auto& s : syms) {
    arr.push_back(doc_symbol_to_json(snap->rope, s, mk));
  }
  tr.send_response(id, JsonValue{std::move(arr)});
}

void LspServer::handle_selection_range(const JsonValue& id,
                                       const JsonValue& params,
                                       LspTransport& tr) {
  std::string uri(params.at("textDocument").at("uri").as_string());
  auto snap = store_.ensure_parsed(uri);
  if (!snap) {
    tr.send_response(id, JsonValue{JsonArray{}});
    return;
  }

  JsonArray out;
  for (const auto& pos : params.at("positions").as_array()) {
    uint32_t b = lsp_pos_to_byte(snap->rope, pos);
    auto ranges = Services::expand_selection(*snap, b);
    JsonValue cur{nullptr};
    for (auto it = ranges.rbegin(); it != ranges.rend(); ++it) {
      JsonObject sr;
      sr["range"] = make_lsp_range(snap->rope, lx_range{it->start, it->end});
      if (!cur.is_null()) {
        sr["parent"] = std::move(cur);
      }
      cur = JsonValue{std::move(sr)};
    }
    if (!cur.is_null()) out.push_back(std::move(cur));
  }
  tr.send_response(id, JsonValue{std::move(out)});
}

}  // namespace lx::host
