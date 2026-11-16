#include "language.hpp"
#include "platform.h"
#include "host/session.hpp"
#include "host/services.hpp"
#include "host/json.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {
using lx::host::Session;
struct Document {
    std::shared_ptr<Session> session;
    std::string text;
    J *report = nullptr;
    ~Document() { json_free(report); }
};
struct ModuleInfo {
    std::vector<std::string> extensions;
    std::filesystem::path library;
    std::string binary_hash;
};
std::filesystem::path utf8_path(const char *s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}
std::string extension(const char *path) {
    auto ext = utf8_path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}
J *position(const lx::Rope &rope, uint32_t byte) {
    const auto line = rope.byte_to_point(byte).line;
    J *p = j_obj();
    j_set(p, "line", j_int(line));
    j_set(p, "character", j_int(rope.byte_to_utf16(byte) - rope.byte_to_utf16(rope.line_start_byte(line))));
    return p;
}
J *range(const lx::Rope &rope, lx_range r) {
    J *p = j_obj();
    j_set(p, "start", position(rope, r.start));
    j_set(p, "end", position(rope, r.end));
    return p;
}
constexpr const char *kinds[] = {
    "symbol",   "file",       "module",      "namespace", "package",   "class",        "method",
    "property", "field",      "constructor", "enum",      "interface", "function",     "variable",
    "constant", "string",     "number",      "boolean",   "array",     "object",       "key",
    "null",     "enummember", "struct",      "event",     "operator",  "typeparameter"};
void symbols(const lx::Rope &rope, const std::vector<lx::host::DocumentSymbolNode> &nodes,
             const std::string &scope, bool nested, SymbolList *out) {
    for (const auto &n : nodes) {
        if (out->n == out->cap) {
            out->cap = out->cap ? out->cap * 2 : 16;
            out->v = static_cast<Symbol *>(na_realloc(out->v, out->cap * sizeof(Symbol)));
        }
        Symbol &s = out->v[out->n++];
        s.name = na_strdup(n.name.c_str());
        s.kind = na_strdup(n.detail == "declaration"     ? "declaration"
                           : n.kind >= 1 && n.kind <= 26 ? kinds[n.kind]
                                                         : kinds[0]);
        s.scope = na_strdup(scope.c_str());
        s.start = rope.byte_to_char(n.range.start);
        s.end = rope.byte_to_char(n.range.end);
        s.sel = rope.byte_to_char(n.selection_range.start);
        auto point = rope.byte_to_point(n.range.start);
        s.line = point.line + 1;
        s.column = rope.byte_to_char(n.range.start) - rope.byte_to_char(rope.line_start_byte(point.line));
        if (nested)
            symbols(rope, n.children, scope.empty() ? n.name : scope + "::" + n.name, true, out);
    }
}
} // namespace

struct LanguageEngine {
    struct Runtime {
        lx::host::LanguageRegistry registry;
        std::unordered_map<std::string, ModuleInfo> modules;
        std::once_flag indexed;
    };
    static Runtime &runtime() {
        static Runtime value;
        return value;
    }
    lx::host::LanguageRegistry &registry = runtime().registry;
    std::unordered_map<std::string, ModuleInfo> &modules = runtime().modules;
    std::unordered_map<std::string, std::unique_ptr<Document>> documents;
    std::string root;
    uint64_t cache_hits = 0, cache_misses = 0;
    static std::string executable_hash(){
        static const std::string value=[](){
#ifdef __linux__
            std::ifstream input("/proc/self/exe",std::ios::binary);
#else
            std::unique_ptr<char,decltype(&free)> executable(pf_executable(),free);if(!executable)return std::string{};
            std::ifstream input(utf8_path(executable.get()),std::ios::binary);
#endif
            if(!input)return std::string{};std::string bytes((std::istreambuf_iterator<char>(input)),{});return hash(bytes);
        }();return value;
    }
    static std::mutex &cache_mutex() {
        static std::mutex value;
        return value;
    }
    static std::string hash(std::string_view text) {
        uint8_t bytes[16];
        char encoded[33];
        blake2b16(text.data(), text.size(), bytes);
        hex_encode(bytes, 16, encoded);
        return encoded;
    }
    std::filesystem::path cache_path(const char *path, const char *text, size_t len, const char *kind) {
        if (root.empty()||executable_hash().empty())
            return {};
        auto id = registry.resolve_language_id_by_extension(extension(path));
        if (!id)
            return {};
        std::string binary;
        {
            std::lock_guard lock(cache_mutex());
            auto &module = modules.at(*id);
            if (module.binary_hash.empty()) {
                std::ifstream input(module.library, std::ios::binary);
                if (!input)
                    return {};
                std::string bytes((std::istreambuf_iterator<char>(input)), {});
                module.binary_hash = hash(bytes);
            }
            binary = module.binary_hash;
        }
        auto folder = utf8_path(root.c_str()) / ".aed" / "native-cache";
        auto encoded = folder.u8string();
        std::string native(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        auto parent = folder.parent_path().u8string();
        std::string native_parent(reinterpret_cast<const char *>(parent.data()), parent.size());
        if (pf_is_reparse(native.c_str()) || pf_is_reparse(native_parent.c_str()))
            return {};
        auto key = hash(std::string(kind) + ":" + *id + ":" + binary + ":" + executable_hash() + ":" +
                        hash(std::string_view(text, len)));
        return folder / (key + ".json");
    }
    J *cache_read(const char *path, const char *text, size_t len, const char *kind) {
        auto file = cache_path(path, text, len, kind);
        if (file.empty()) {
            cache_misses++;
            return nullptr;
        }
        auto bytes = file.u8string();
        std::string name(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        PfStat stat{};
        if (!pf_stat(name.c_str(), &stat) || stat.size > 16 * 1024 * 1024 || pf_is_reparse(name.c_str())) {
            cache_misses++;
            return nullptr;
        }
        Buf data;
        buf_init(&data);
        int rc = pf_read_file(name.c_str(), &data);
        J *record = rc == 0 ? json_parse(data.p, data.len) : nullptr;
        buf_free(&data);
        const J *payload = j_get(record, "payload"), *checksum = j_get(record, "checksum");
        char *serialized = payload ? json_dump(payload) : nullptr;
        bool valid = serialized && j_is_str(checksum) && hash(serialized) == checksum->s;
        J *copy = valid ? json_parse(serialized, strlen(serialized)) : nullptr;
        free(serialized);
        json_free(record);
        if (copy)
            cache_hits++;
        else
            cache_misses++;
        return copy;
    }
    void cache_write(const char *path, const char *text, size_t len, const char *kind, const J *value) {
        auto file = cache_path(path, text, len, kind);
        if (file.empty())
            return;
        auto dir = file.parent_path().u8string(), full = file.u8string();
        std::string folder(reinterpret_cast<const char *>(dir.data()), dir.size()),
            name(reinterpret_cast<const char *>(full.data()), full.size());
        if (pf_is_reparse(name.c_str()) || pf_mkdirs(folder.c_str()))
            return;
        char *body = json_dump(value);
        J *record = j_obj();
        j_set(record, "checksum", j_str(hash(body).c_str()));
        j_set(record, "payload", json_parse(body, strlen(body)));
        free(body);
        char *encoded = json_dump(record);
        pf_write_atomic(name.c_str(), encoded, strlen(encoded));
        free(encoded);
        json_free(record);
    }

    std::string error;

    explicit LanguageEngine(const char *workspace) : root(workspace ? workspace : "") {
        std::call_once(runtime().indexed, [this] {
            std::filesystem::path plugin_dir;
            if (const char *dir = getenv("NEXTAGENT_PLUGIN_DIR"))
                plugin_dir = utf8_path(dir);
            else if (const char *dir = getenv("NEXTAGENT_LANGSERVER_DIR"))
                plugin_dir = utf8_path(dir) / "plugins";
            else {
                std::unique_ptr<char, decltype(&free)> self(pf_executable(), free);
                if (self)
                    plugin_dir = utf8_path(self.get()).parent_path() / "plugins";
            }
            if (plugin_dir.empty() || !std::filesystem::is_directory(plugin_dir))
                return;
            for (const auto &entry : std::filesystem::directory_iterator(plugin_dir)) {
                if (!entry.is_regular_file() ||
                    !entry.path().filename().string().ends_with(".lxmanifest.json"))
                    continue;
                std::string err;
                if (!registry.register_manifest(entry.path(), &err)) {
                    error = err;
                    continue;
                }
                std::ifstream file(entry.path(), std::ios::binary);
                std::string data((std::istreambuf_iterator<char>(file)), {});
                auto manifest = lx::host::parse_json(data);
                if (!manifest)
                    continue;
                const auto &value = *manifest.value;
                ModuleInfo info;
                for (const auto &ext : value.at("extensions").as_array())
                    info.extensions.emplace_back(ext.as_string());
                info.library = plugin_dir / utf8_path(std::string(value.at("library").as_string()).c_str());
                modules.emplace(value.at("id").as_string(), std::move(info));
            }
        });
    }

    Document *analyze(const char *path, const char *text, size_t len, int64_t version) {
        try {
            if (len > std::numeric_limits<uint32_t>::max())
                throw std::runtime_error("document too large");
            auto &slot = documents[path];
            if (!slot)
                slot = std::make_unique<Document>();
            auto &d = *slot;
            if (!d.session) {
                std::string err;
                auto module = registry.get_or_load_for_uri(path, &err);
                if (!module)
                    throw std::runtime_error(err.empty() ? "no module for file extension" : err);
                d.session = std::make_shared<Session>(path, module, std::string_view(text, len), registry);
            } else if (d.text != std::string_view(text, len)) {
                size_t prefix = 0, suffix = 0;
                while (prefix < len && prefix < d.text.size() && text[prefix] == d.text[prefix])
                    ++prefix;
                while (prefix && prefix < len && (static_cast<unsigned char>(text[prefix]) & 0xc0) == 0x80)
                    --prefix;
                while (suffix < len - prefix && suffix < d.text.size() - prefix &&
                       text[len - suffix - 1] == d.text[d.text.size() - suffix - 1])
                    ++suffix;
                while (suffix && (static_cast<unsigned char>(text[len - suffix]) & 0xc0) == 0x80)
                    --suffix;
                d.session->apply_byte_edit(uint32_t(prefix), uint32_t(d.text.size() - suffix),
                                           std::string_view(text + prefix, len - prefix - suffix));
            }
            d.text.assign(text, len);
            if (d.session->reparse() != LX_OK)
                throw std::runtime_error("language parse failed");
            auto snap = d.session->snapshot();
            if (!snap || !snap->root)
                throw std::runtime_error("language parser returned no syntax tree");
            J *report = j_obj(), *diags = j_arr();
            j_set(report, "version", j_int(version));
            j_set(report, "snapshot_version", j_int(static_cast<int64_t>(snap->version)));
            j_set(report, "language", j_str(snap->module->id.c_str()));
            for (const auto &diagnostic : d.session->layered_cursor().all_diagnostics()) {
                J *item = j_obj();
                j_set(item, "severity", j_int(int(diagnostic.severity)));
                j_set(item, "code", j_str(diagnostic.code.c_str()));
                j_set(item, "message", j_str(diagnostic.message.c_str()));
                j_set(item, "range", range(snap->rope, diagnostic.range));
                j_push(diags, item);
            }
            j_set(report, "diagnostics", diags);
            json_free(d.report);
            d.report = report;
            error.clear();
            return &d;
        } catch (const std::exception &e) {
            error = e.what();
            na_seterr("language_analysis_failed: %s: %s", path, error.c_str());
            return nullptr;
        }
    }
};

LanguageEngine *language_new(const char *root) {
    try {
        return new LanguageEngine(root);
    } catch (const std::exception &e) {
        na_seterr("language_engine_failed: %s", e.what());
        return nullptr;
    }
}
void language_free(LanguageEngine *engine) { delete engine; }

bool language_configured(LanguageEngine *engine, const char *path) {
    return engine && engine->registry.resolve_language_id_by_extension(extension(path)).has_value();
}
bool language_handles(LanguageEngine *engine, const char *path) { return language_configured(engine, path); }
int language_outline(LanguageEngine *engine, const char *path, const char *text, size_t len, bool nested,
                     SymbolList *out) {
    *out = {};
    const char *cache_kind = nested ? "outline-nested-v3" : "outline-top-v3";
    if (J *cached = engine->cache_read(path, text, len, cache_kind)) {
        bool valid = cached->k == J_ARR;
        for (size_t i = 0; valid && i < cached->n; i++) {
            const J *entry = cached->items[i], *start = j_get(entry, "start"), *end = j_get(entry, "end"),
                    *sel = j_get(entry, "sel"), *line = j_get(entry, "line"), *col = j_get(entry, "column");
            valid = j_is_int(start) && j_is_int(end) && j_is_int(sel) && j_is_int(line) && j_is_int(col) &&
                    start->i >= 0 && end->i >= start->i && uint64_t(end->i) <= utf8_cp_count(text, len) &&
                    j_is_str(j_get(entry, "name")) && j_is_str(j_get(entry, "kind")) &&
                    j_is_str(j_get(entry, "scope"));
        }
        if (valid) {
            out->n = out->cap = cached->n;
            out->v = static_cast<Symbol *>(na_alloc((out->n ? out->n : 1) * sizeof(Symbol)));
            for (size_t i = 0; i < out->n; i++) {
                const J *entry = cached->items[i];
                auto &sym = out->v[i];
                sym.name = na_strdup(j_get(entry, "name")->s);
                sym.kind = na_strdup(j_get(entry, "kind")->s);
                sym.scope = na_strdup(j_get(entry, "scope")->s);
                sym.start = j_get(entry, "start")->i;
                sym.end = j_get(entry, "end")->i;
                sym.sel = j_get(entry, "sel")->i;
                sym.line = j_get(entry, "line")->i;
                sym.column = j_get(entry, "column")->i;
            }
            json_free(cached);
            return 0;
        }
        json_free(cached);
    }
    auto d = engine->analyze(path, text, len, 0);
    if (!d)
        return -1;
    auto snap = d->session->snapshot();
    auto list = lx::host::Services::outline(*snap);
    symbols(snap->rope, list, "", nested, out);
    J *cached = j_arr();
    for (size_t i = 0; i < out->n; i++) {
        J *entry = symbol_to_json(&out->v[i]);
        j_set(entry, "sel", j_int(out->v[i].sel));
        j_push(cached, entry);
    }
    engine->cache_write(path, text, len, cache_kind, cached);
    json_free(cached);
    return 0;
}
J *language_status(LanguageEngine *engine) {
    J *out = j_arr();
    for (const auto &id : engine->registry.registered_languages()) {
        J *item = j_obj(), *extensions = j_arr();
        const auto &info = engine->modules.at(id);
        for (const auto &ext : info.extensions)
            j_push(extensions, j_str(ext.c_str()));
        j_set(item, "name", j_str(id.c_str()));
        j_set(item, "extensions", extensions);
        j_set(item, "mode", j_str("in_process"));
        j_set(item, "loaded", j_bool(bool(engine->registry.find_loaded(id))));
        j_set(item, "available", j_bool(std::filesystem::is_regular_file(info.library)));
        J *caps = j_obj();
        j_set(caps, "syntax_diagnostics", j_bool(true));
        j_set(caps, "outline", j_bool(true));
        j_set(caps, "ast_labels", j_bool(true));
        j_set(caps, "semantic_references", j_bool(false));
        j_set(caps, "semantic_rename", j_bool(false));
        j_set(caps, "persistent_derived_cache", j_bool(!engine->root.empty()));
        j_set(item, "capabilities", caps);
        J *limits = j_arr();
        j_push(limits, j_str("Syntax grammar coverage only; no compiler-level semantic binding"));
        if (id == "mermaid")
            j_push(limits, j_str("ER and class diagrams only; flowcharts unsupported"));
        j_set(item, "limitations", limits);
        j_set(item,"cache_scope",j_str("workspace language engine totals"));
        j_set(item, "cache_hits", j_int(engine->cache_hits));
        j_set(item, "cache_misses", j_int(engine->cache_misses));
        if (!engine->error.empty())
            j_set(item, "error", j_str(engine->error.c_str()));
        j_push(out, item);
    }
    return out;
}
PosEnc language_encoding(LanguageEngine *, const char *) { return ENC_UTF16; }
int language_sync(LanguageEngine *engine, const char *path, const char *text, size_t len) {
    return engine->analyze(path, text, len, 0) ? 0 : -1;
}
void language_settle(LanguageEngine *, const char *) {} // reparses are synchronous
int language_references(LanguageEngine *engine, const char *path, const char *, size_t, size_t, bool,
                        SymbolLocations *out) {
    *out = {};
    auto id = engine->registry.resolve_language_id_by_extension(extension(path));
    if (id && (*id == "c" || *id == "cpp"))
        return na_fail("no_language_server: use C/C++ syntax refactorings");
    return na_fail("references_unsupported: language modules do not provide semantic binding");
}
int language_rename(LanguageEngine *engine, const char *path, const char *text, size_t len, size_t offset,
                    const char *, SymbolLocations *out) {
    return language_references(engine, path, text, len, offset, true, out);
}
void symbol_locations_free(SymbolLocations *locations) {
    for (size_t i = 0; i < locations->n; i++) {
        free(locations->v[i].path);
        free(locations->v[i].text);
    }
    free(locations->v);
    *locations = {};
}

namespace {
int open_document(void *ctx, const char *path, const char *text, size_t len, int64_t version, char **uri) {
    auto engine = static_cast<LanguageEngine *>(ctx);
    if (!engine->analyze(path, text, len, version))
        return -1;
    *uri = na_strdup(path);
    return 0;
}
int change_document(void *ctx, const char *path, const char *text, size_t len, int64_t version) {
    return static_cast<LanguageEngine *>(ctx)->analyze(path, text, len, version) ? 0 : -1;
}
int wait_diagnostics(void *ctx, const char *path, int64_t version, J **report) {
    auto &docs = static_cast<LanguageEngine *>(ctx)->documents;
    auto it = docs.find(path);
    if (it == docs.end())
        return na_fail("document_not_open: %s", path);
    const J *diags;
    if (diag_require_fresh(it->second->report, version, &diags) != 0)
        return -1;
    std::unique_ptr<char, decltype(&free)> raw(json_dump(it->second->report), free);
    *report = json_parse(raw.get(), strlen(raw.get()));
    return *report ? 0 : -1;
}
int close_document(void *ctx, const char *path) {
    static_cast<LanguageEngine *>(ctx)->documents.erase(path);
    return 0;
}
bool supports(void *ctx, const char *path) {
    return language_handles(static_cast<LanguageEngine *>(ctx), path);
}
} // namespace
DiagnosticOps language_ops(LanguageEngine *engine) {
    return {open_document, change_document, wait_diagnostics, close_document, engine, supports};
}

int language_nodes(LanguageEngine *engine, const char *path, const char *text, size_t len, J **out) {
    if (J *cached = engine->cache_read(path, text, len, "ast-nodes-v1")) {
        const J *nodes = j_get(cached, "nodes");
        bool valid = nodes && nodes->k == J_ARR;
        for (size_t i = 0; valid && i < nodes->n; i++) {
            const J *n = nodes->items[i], *a = j_get(n, "start"), *b = j_get(n, "end");
            valid = j_is_int(a) && j_is_int(b) && a->i >= 0 && b->i >= a->i &&
                    uint64_t(b->i) <= utf8_cp_count(text, len) && j_is_str(j_get(n, "node_kind")) &&
                    j_is_str(j_get(n, "fingerprint"));
        }
        if (valid) {
            *out = cached;
            return 0;
        }
        json_free(cached);
    }
    auto doc = engine->analyze(path, text, len, 0);
    if (!doc)
        return -1;
    auto snap = doc->session->snapshot();
    J *nodes = j_arr();
    bool truncated = false;
    std::function<void(const lx::uast::GreenNode *, uint32_t, int, size_t)> visit =
        [&](auto node, uint32_t off, int parent, size_t depth) {
            if (!node || nodes->n >= 20000 || depth > 512) {
                truncated = true;
                return;
            }
            if (node->is_token() || node->is_error() || node->is_missing())
                return;
            int next = parent;
            if (node->is_rule() && !node->is_seq() && node->width) {
                auto kind = snap->module->symbol_name(lx::uast::kind_symbol(node->kind));
                if (!kind.empty()) {
                    J *entry = j_obj();
                    j_set(entry, "index", j_int(nodes->n));
                    j_set(entry, "parent", parent < 0 ? j_null() : j_int(parent));
                    j_set(entry, "node_kind", j_strn(kind.data(), kind.size()));
                    j_set(entry, "start", j_int(snap->rope.byte_to_char(off)));
                    j_set(entry, "end", j_int(snap->rope.byte_to_char(off + node->width)));
                    uint8_t hash[16];
                    char hx[33];
                    blake2b16(text + off, node->width, hash);
                    hex_encode(hash, 16, hx);
                    j_set(entry, "fingerprint", j_str(hx));
                    next = int(nodes->n);
                    j_push(nodes, entry);
                }
            }
            if (node->is_inner())
                for (auto &child : node->as_inner()->children()) {
                    if (child.node) {
                        visit(child.node, off, next, depth + 1);
                        off += child.node->width;
                    }
                }
        };
    visit(snap->root.get(), 0, -1, 0);
    J *report = j_obj();
    j_set(report, "language", j_str(snap->module->id.c_str()));
    j_set(report, "nodes", nodes);
    j_set(report, "truncated", j_bool(truncated));
    engine->cache_write(path, text, len, "ast-nodes-v1", report);
    *out = report;
    return 0;
}
