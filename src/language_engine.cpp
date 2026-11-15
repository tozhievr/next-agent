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
};
std::filesystem::path utf8_path(const char *s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}
std::string extension(const char *path) {
    auto ext = utf8_path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}
J *position(const lx::Rope& rope, uint32_t byte) {
    const auto line = rope.byte_to_point(byte).line;
    J *p = j_obj();
    j_set(p, "line", j_int(line));
    j_set(p, "character", j_int(rope.byte_to_utf16(byte) - rope.byte_to_utf16(rope.line_start_byte(line))));
    return p;
}
J *range(const lx::Rope& rope, lx_range r) {
    J *p = j_obj();
    j_set(p, "start", position(rope, r.start));
    j_set(p, "end", position(rope, r.end));
    return p;
}
constexpr const char *kinds[] = {
    "symbol", "file", "module", "namespace", "package", "class", "method", "property", "field",
    "constructor", "enum", "interface", "function", "variable", "constant", "string", "number",
    "boolean", "array", "object", "key", "null", "enummember", "struct", "event", "operator", "typeparameter"};
void symbols(const lx::Rope& rope, const std::vector<lx::host::DocumentSymbolNode>& nodes,
             const std::string& scope, bool nested, SymbolList *out) {
    for (const auto& n : nodes) {
        if (out->n == out->cap) {
            out->cap = out->cap ? out->cap * 2 : 16;
            out->v = static_cast<Symbol *>(na_realloc(out->v, out->cap * sizeof(Symbol)));
        }
        Symbol& s = out->v[out->n++];
        s.name = na_strdup(n.name.c_str());
        s.kind = na_strdup(n.detail == "declaration" ? "declaration" : n.kind >= 1 && n.kind <= 26 ? kinds[n.kind] : kinds[0]);
        s.scope = na_strdup(scope.c_str());
        s.start = rope.byte_to_char(n.range.start);
        s.end = rope.byte_to_char(n.range.end);
        s.sel = rope.byte_to_char(n.selection_range.start);
        auto point = rope.byte_to_point(n.range.start);
        s.line = point.line + 1;
        s.column = rope.byte_to_char(n.range.start) - rope.byte_to_char(rope.line_start_byte(point.line));
        if (nested) symbols(rope, n.children, scope.empty() ? n.name : scope + "::" + n.name, true, out);
    }
}
}

struct LanguageEngine {
    struct Runtime {
        lx::host::LanguageRegistry registry;
        std::unordered_map<std::string, ModuleInfo> modules;
        std::once_flag indexed;
    };
    static Runtime& runtime() { static Runtime value; return value; }
    lx::host::LanguageRegistry& registry = runtime().registry;
    std::unordered_map<std::string, ModuleInfo>& modules = runtime().modules;
    std::unordered_map<std::string, std::unique_ptr<Document>> documents;

    std::string error;

    explicit LanguageEngine(const char *) {
        std::call_once(runtime().indexed, [this] {
        std::filesystem::path plugin_dir;
        if (const char *dir = getenv("NEXTAGENT_PLUGIN_DIR")) plugin_dir = utf8_path(dir);
        else if (const char *dir = getenv("NEXTAGENT_LANGSERVER_DIR")) plugin_dir = utf8_path(dir) / "plugins";
        else {
            std::unique_ptr<char, decltype(&free)> self(pf_executable(), free);
            if (self) plugin_dir = utf8_path(self.get()).parent_path() / "plugins";
        }
        if (plugin_dir.empty() || !std::filesystem::is_directory(plugin_dir)) return;
        for (const auto& entry : std::filesystem::directory_iterator(plugin_dir)) {
            if (!entry.is_regular_file() || !entry.path().filename().string().ends_with(".lxmanifest.json")) continue;
            std::string err;
            if (!registry.register_manifest(entry.path(), &err)) { error = err; continue; }
            std::ifstream file(entry.path(), std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(file)), {});
            auto manifest = lx::host::parse_json(data);
            if (!manifest) continue;
            const auto& value = *manifest.value;
            ModuleInfo info;
            for (const auto& ext : value.at("extensions").as_array()) info.extensions.emplace_back(ext.as_string());
            info.library = plugin_dir / utf8_path(std::string(value.at("library").as_string()).c_str());
            modules.emplace(value.at("id").as_string(), std::move(info));
        }
        });
    }

    Document *analyze(const char *path, const char *text, size_t len, int64_t version) {
        try {
            if (len > std::numeric_limits<uint32_t>::max()) throw std::runtime_error("document too large");
            auto& slot = documents[path];
            if (!slot) slot = std::make_unique<Document>();
            auto& d = *slot;
            if (!d.session) {
                std::string err;
                auto module = registry.get_or_load_for_uri(path, &err);
                if (!module) throw std::runtime_error(err.empty() ? "no module for file extension" : err);
                d.session = std::make_shared<Session>(path, module, std::string_view(text, len), registry);
            } else if (d.text != std::string_view(text, len)) {
                size_t prefix = 0, suffix = 0;
                while (prefix < len && prefix < d.text.size() && text[prefix] == d.text[prefix]) ++prefix;
                while (prefix && prefix < len && (static_cast<unsigned char>(text[prefix]) & 0xc0) == 0x80) --prefix;
                while (suffix < len - prefix && suffix < d.text.size() - prefix &&
                       text[len - suffix - 1] == d.text[d.text.size() - suffix - 1]) ++suffix;
                while (suffix && (static_cast<unsigned char>(text[len - suffix]) & 0xc0) == 0x80) --suffix;
                d.session->apply_byte_edit(uint32_t(prefix), uint32_t(d.text.size() - suffix),
                                          std::string_view(text + prefix, len - prefix - suffix));
            }
            d.text.assign(text, len);
            if (d.session->reparse() != LX_OK) throw std::runtime_error("language parse failed");
            auto snap = d.session->snapshot();
            if (!snap || !snap->root) throw std::runtime_error("language parser returned no syntax tree");
            J *report = j_obj(), *diags = j_arr();
            j_set(report, "version", j_int(version));
            j_set(report, "snapshot_version", j_int(static_cast<int64_t>(snap->version)));
            j_set(report, "language", j_str(snap->module->id.c_str()));
            for (const auto& diagnostic : d.session->layered_cursor().all_diagnostics()) {
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
        } catch (const std::exception& e) {
            error = e.what();
            na_seterr("language_analysis_failed: %s: %s", path, error.c_str());
            return nullptr;
        }
    }
};

LanguageEngine *language_new(const char *root) {
    try { return new LanguageEngine(root); }
    catch (const std::exception& e) { na_seterr("language_engine_failed: %s", e.what()); return nullptr; }
}
void language_free(LanguageEngine *engine) { delete engine; }

bool language_configured(LanguageEngine *engine, const char *path) {
    return engine && engine->registry.resolve_language_id_by_extension(extension(path)).has_value();
}
bool language_handles(LanguageEngine *engine, const char *path) { return language_configured(engine, path); }
int language_outline(LanguageEngine *engine, const char *path, const char *text, size_t len, bool nested, SymbolList *out) {
    *out = {};
    auto d = engine->analyze(path, text, len, 0);
    if (!d) return -1;
    auto snap = d->session->snapshot();
    auto list = lx::host::Services::outline(*snap);
    symbols(snap->rope, list, "", nested, out);
    return 0;
}
J *language_status(LanguageEngine *engine) {
    J *out = j_arr();
    for (const auto& id : engine->registry.registered_languages()) {
        J *item = j_obj(), *extensions = j_arr();
        const auto& info = engine->modules.at(id);
        for (const auto& ext : info.extensions) j_push(extensions, j_str(ext.c_str()));
        j_set(item, "name", j_str(id.c_str()));
        j_set(item, "extensions", extensions);
        j_set(item, "mode", j_str("in_process"));
        j_set(item, "loaded", j_bool(bool(engine->registry.find_loaded(id))));
        j_set(item, "available", j_bool(std::filesystem::is_regular_file(info.library)));
        if (!engine->error.empty()) j_set(item, "error", j_str(engine->error.c_str()));
        j_push(out, item);
    }
    return out;
}
PosEnc language_encoding(LanguageEngine *, const char *) { return ENC_UTF16; }
int language_sync(LanguageEngine *engine, const char *path, const char *text, size_t len) {
    return engine->analyze(path, text, len, 0) ? 0 : -1;
}
void language_settle(LanguageEngine *, const char *) {} // reparses are synchronous
int language_references(LanguageEngine *engine, const char *path, const char *, size_t, size_t, bool, SymbolLocations *out) {
    *out = {};
    auto id = engine->registry.resolve_language_id_by_extension(extension(path));
    if (id && (*id == "c" || *id == "cpp")) return na_fail("no_language_server: use C/C++ syntax refactorings");
    return na_fail("references_unsupported: language modules do not provide semantic binding");
}
int language_rename(LanguageEngine *engine, const char *path, const char *text, size_t len, size_t offset,
                    const char *, SymbolLocations *out) {
    return language_references(engine, path, text, len, offset, true, out);
}
void symbol_locations_free(SymbolLocations *locations) {
    for (size_t i = 0; i < locations->n; i++) { free(locations->v[i].path); free(locations->v[i].text); }
    free(locations->v);
    *locations = {};
}

namespace {
int open_document(void *ctx, const char *path, const char *text, size_t len, int64_t version, char **uri) {
    auto engine = static_cast<LanguageEngine *>(ctx);
    if (!engine->analyze(path, text, len, version)) return -1;
    *uri = na_strdup(path);
    return 0;
}
int change_document(void *ctx, const char *path, const char *text, size_t len, int64_t version) {
    return static_cast<LanguageEngine *>(ctx)->analyze(path, text, len, version) ? 0 : -1;
}
int wait_diagnostics(void *ctx, const char *path, int64_t version, J **report) {
    auto& docs = static_cast<LanguageEngine *>(ctx)->documents;
    auto it = docs.find(path);
    if (it == docs.end()) return na_fail("document_not_open: %s", path);
    const J *diags;
    if (diag_require_fresh(it->second->report, version, &diags) != 0) return -1;
    std::unique_ptr<char, decltype(&free)> raw(json_dump(it->second->report), free);
    *report = json_parse(raw.get(), strlen(raw.get()));
    return *report ? 0 : -1;
}
int close_document(void *ctx, const char *path) {
    static_cast<LanguageEngine *>(ctx)->documents.erase(path);
    return 0;
}
bool supports(void *ctx, const char *path) { return language_handles(static_cast<LanguageEngine *>(ctx), path); }
}
DiagnosticOps language_ops(LanguageEngine *engine) {
    return {open_document, change_document, wait_diagnostics, close_document, engine, supports};
}
