#include "agent.h"
#include "host/json.hpp"
#include "platform.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <functional>
#include <cctype>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using V = lx::host::JsonValue;
using O = lx::host::JsonObject;
using A = lx::host::JsonArray;
V decode(const J *value) {
    if (!value)
        return V(nullptr);
    char *text = json_dump(value);
    auto parsed = lx::host::parse_json(text);
    free(text);
    if (!parsed)
        throw std::runtime_error("Invalid native JSON");
    return std::move(*parsed.value);
}
J *encode(const V &value) {
    auto text = value.dump();
    return json_parse(text.data(), text.size());
}
std::string str(const V &v, std::string_view key) { return std::string(v.at(key).as_string()); }
std::string fingerprint(std::string_view text) {
    uint8_t bytes[16];
    char out[33];
    blake2b16(text.data(), text.size(), bytes);
    hex_encode(bytes, 16, out);
    return out;
}
void check(int rc) {
    if (rc)
        throw std::runtime_error(na_errmsg());
}
std::string required(const V &p, std::string_view key) {
    if (!p.at(key).is_string() || p.at(key).as_string().empty())
        throw std::runtime_error("Required string: " + std::string(key));
    return str(p, key);
}
std::string canon(Rpc *r, const std::string &path) {
    std::unique_ptr<char, decltype(&free)> value(store_canon(r->store, path.c_str()), free);
    if (!value)
        throw std::runtime_error(na_errmsg());
    return value.get();
}
V meta(Rpc *r, const std::string &key) {
    J *v = nullptr;
    check(store_meta_get(r->store, key.c_str(), &v));
    auto result = decode(v);
    json_free(v);
    return result;
}
void put(Rpc *r, const std::string &key, const V &v, const char *expected = nullptr) {
    std::unique_ptr<J, decltype(&json_free)> native(encode(v), json_free);
    check(store_meta_put(r->store, key.c_str(), native.get(), expected));
}
struct Engine {
    LanguageEngine *value;
    bool owned;
    explicit Engine(Rpc *r)
        : value(r->languages ? r->languages : language_new(store_root(r->store))), owned(!r->languages) {
        if (!value)
            throw std::runtime_error(na_errmsg());
    }
    ~Engine() {
        if (owned)
            language_free(value);
    }
};
using Snap = std::unique_ptr<Snapshot, decltype(&snap_free)>;
Snap current(Rpc *r, const std::string &path) {
    check(agent_reconcile(r, path.c_str()));
    Snapshot *s = nullptr;
    check(store_view(r->store, path.c_str(), &s));
    return Snap(s, snap_free);
}
std::string slice(const Snapshot *s, size_t start, size_t end) {
    auto b = cp_to_byte(s->text, s->len, start), e = cp_to_byte(s->text, s->len, end);
    if (b == size_t(-1) || e == size_t(-1) || e < b)
        throw std::runtime_error("AST range outside source");
    return std::string(s->text + b, e - b);
}
V nodes(Rpc *r, const std::string &path, const Snapshot *snap) {
    Engine engine(r);
    J *out = nullptr;
    auto absolute = std::string(store_root(r->store)) + "/" + path;
    check(language_nodes(engine.value, absolute.c_str(), snap->text, snap->len, &out));
    auto value = decode(out);
    json_free(out);
    if (value.at("truncated").as_bool())
        throw std::runtime_error("AST limit exceeded; query a smaller file");
    SymbolList symbols{};
    check(language_outline(engine.value, absolute.c_str(), snap->text, snap->len, true, &symbols));
    for (auto &n : value["nodes"].as_array_mut()) {
        const Symbol *owner = nullptr;
        size_t width = size_t(-1);
        auto start = n.at("start").as_uint64(), end = n.at("end").as_uint64();
        for (size_t i = 0; i < symbols.n; i++) {
            auto &sym = symbols.v[i];
            if (sym.start <= start && sym.end >= end && sym.end - sym.start < width) {
                owner = &sym;
                width = sym.end - sym.start;
            }
        }
        if (owner)
            n["owner"] = O{{"name", owner->name}, {"kind", owner->kind}, {"scope", owner->scope}};
        n["path"] = path;
        n["rev"] = snap->rev;
    }
    symbols_free(&symbols);
    return value;
}
V descriptor(const V &node) {
    return O{{"path", node.at("path")},
             {"node_kind", node.at("node_kind")},
             {"fingerprint", node.at("fingerprint")},
             {"owner", node.at("owner")}};
}
std::string remember(Rpc *r, V &node) {
    auto d = descriptor(node);
    auto id = "ast_" + fingerprint(d.dump());
    auto old = meta(r, "anchor/" + id);
    if (old.is_null()) {
        std::unique_ptr<J, decltype(&json_free)> native(encode(d), json_free);
        if (store_meta_put(r->store, ("anchor/" + id).c_str(), native.get(), "missing") != 0) {
            auto raced = meta(r, "anchor/" + id);
            if (raced.dump() != d.dump())
                throw std::runtime_error(na_errmsg());
        }
    }
    node["anchor"] = id;
    return id;
}
V resolve(Rpc *r, const std::string &input) {
    auto id = input;
    auto binding = input.starts_with("ast_")?V(nullptr):meta(r, "label/" + input);
    if (!binding.is_null())
        id = required(binding, "anchor");
    auto d = meta(r, "anchor/" + id);
    if (d.is_null())
        throw std::runtime_error("unknown_anchor: " + input);
    auto path = required(d, "path");
    auto snap = current(r, path);
    auto tree = nodes(r, path, snap.get());
    A matches;
    for (auto &n : tree.at("nodes").as_array())
        if (descriptor(n).dump() == d.dump())
            matches.push_back(n);
    if (matches.empty())
        throw std::runtime_error("stale_anchor: AST node changed or was removed: " + input);
    if (matches.size() != 1)
        throw std::runtime_error("ambiguous_anchor: identical AST nodes in the same owner: " + input);
    auto node = matches.front();
    node["anchor"] = id;
    node["text"] = slice(snap.get(), node.at("start").as_uint64(), node.at("end").as_uint64());
    return node;
}
V labels(Rpc *r, const V &p) {
    auto action = str(p, "action");
    if (action.empty())
        action = "query";
    if (action == "list") {
        J *list = nullptr;
        check(store_meta_list(r->store, "label/", &list));
        auto result = decode(list);
        json_free(list);
        A bindings;
        for (auto &[key, value] : result.as_object()) {
            auto item = value;
            item["label"] = key.substr(6);
            bindings.push_back(item);
        }
        return O{{"labels", std::move(bindings)}};
    }
    if (action == "resolve")
        return resolve(r, required(p, "label"));
    if (action == "bind" || action == "unbind") {
        if (r->read_only)
            throw std::runtime_error("read_only: label bindings cannot be modified");
        auto name = required(p, "label");
        if(name.starts_with("ast_"))throw std::runtime_error("Reserved label prefix: ast_");
        if (name.size() > 128 || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return c >= 128 || std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == ':';
            }))
            throw std::runtime_error("Invalid label name");
        auto old = meta(r, "label/" + name);
        if (!old.is_null() && str(p, "expected_anchor") != str(old, "anchor"))
            throw std::runtime_error("label_exists: supply expected_anchor to rebind or remove");
        auto h = old.is_null() ? "missing" : fingerprint(old.dump());
        if (action == "unbind") {
            put(r, "label/" + name, V(nullptr), h.c_str());
            return O{{"label", name}, {"removed", true}};
        }
        auto node = resolve(r, required(p, "anchor"));
        put(r, "label/" + name, O{{"anchor", node.at("anchor")}}, h.c_str());
        node["label"] = name;
        return node;
    }
    if (action != "query")
        throw std::runtime_error("Unsupported labels action");
    if (p.at("label").is_string())
        return resolve(r, str(p, "label"));
    auto path = canon(r, required(p, "target"));
    auto snap = current(r, path);
    auto tree = nodes(r, path, snap.get());
    if(!p.at("offset").is_null()&&(!p.at("offset").is_number()||p.at("offset").as_int()<0||p.at("offset").as_double()!=double(p.at("offset").as_int())))throw std::runtime_error("offset must be a non-negative integer");
    auto limit = p.at("limit").is_null() ? 100 : p.at("limit").as_int();
    if (limit < 1 || limit > 1000)
        throw std::runtime_error("limit must be 1..1000");
    uint64_t from = 0, to = snap->ncp;
    if (p.at("within").is_string()) {
        auto owner = resolve(r, str(p, "within"));
        if (str(owner, "path") != path)
            throw std::runtime_error("within anchor belongs to another file");
        from = owner.at("start").as_uint64();
        to = owner.at("end").as_uint64();
    }
    A items;
    size_t matched = 0;
    for (auto n : tree.at("nodes").as_array()) {
        if (n.at("start").as_uint64() < from || n.at("end").as_uint64() > to)
            continue;
        if (p.at("node_kind").is_string() && str(n, "node_kind") != str(p, "node_kind"))
            continue;
        if (p.at("symbol").is_string() && str(n.at("owner"), "name") != str(p, "symbol"))
            continue;
        if (p.at("scope").is_string() && str(n.at("owner"), "scope") != str(p, "scope"))
            continue;
        if (p.at("offset").is_number() && (n.at("start").as_uint64() > p.at("offset").as_uint64() ||
                                           n.at("end").as_uint64() <= p.at("offset").as_uint64()))
            continue;
        ++matched;
        if (items.size() >= size_t(limit))
            continue;
        remember(r, n);
        items.push_back(n);
    }
    return O{{"language", tree.at("language")},
             {"rev", snap->rev},
             {"nodes", std::move(items)},
             {"matched", matched},
             {"truncated", matched > size_t(limit)}};
}
V invoke(Rpc *r, const std::string &method, const V &params) {
    std::unique_ptr<J, decltype(&json_free)> req(
        encode(O{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}}), json_free);
    std::unique_ptr<J, decltype(&json_free)> reply(rpc_dispatch(r, req.get()), json_free);
    auto v = decode(reply.get());
    if (v.find("error"))
        throw std::runtime_error(v.at("error").dump());
    return v.at("result");
}
V anchored_read(Rpc* r,const V& p){
    V request=p;request["with_anchor"]=false;
    auto result=invoke(r,"view",request);auto path=canon(r,required(p,"target"));auto name=required(p,"symbol");
    auto selected=invoke(r,"select",O{{"target",path},{"name",name},{"scope",p.at("scope")},{"kind",p.at("kind")}});
    auto snap=current(r,path);auto tree=nodes(r,path,snap.get());V best;uint64_t width=uint64_t(-1);int best_score=100;
    for(auto candidate:tree.at("nodes").as_array()){
      auto start=candidate.at("start").as_uint64(),end=candidate.at("end").as_uint64();
      auto kind=str(candidate,"node_kind");std::transform(kind.begin(),kind.end(),kind.begin(),[](unsigned char c){return char(std::tolower(c));});
      int score=(kind.find("function")!=std::string::npos||kind.find("class")!=std::string::npos||kind.find("method")!=std::string::npos)?0:kind.find("declaration")!=std::string::npos?1:2;
      if(start<=selected.at("start").as_uint64()&&end>=selected.at("end").as_uint64()&&(end-start<width||(end-start==width&&score<best_score))&&str(candidate.at("owner"),"name")==name){best=candidate;width=end-start;best_score=score;}
    }
    if(best.is_null())throw std::runtime_error("No uniquely addressable AST node covers the selected symbol");
    result["anchor"]=remember(r,best);result["fingerprint"]=best.at("fingerprint");result["node_kind"]=best.at("node_kind");result["start"]=best.at("start");result["end"]=best.at("end");return result;
}
V read_batch(Rpc *r, const V &p) {
    auto &requests = p.at("requests");
    if (!requests.is_array() || requests.as_array().empty() || requests.as_array().size() > 128)
        throw std::runtime_error("requests must contain 1..128 reads");
    auto budget = p.at("budget_bytes").is_null() ? 16000 : p.at("budget_bytes").as_int();
    if (budget < 512 || budget > 1024 * 1024)
        throw std::runtime_error("budget_bytes must be 512..1048576");
    V result = O{{"items", A{}}, {"truncated", false}, {"omitted", 0}};
    size_t index = 0;
    for (auto request : requests.as_array()) {
        V item;
        try {
            if (request.at("label").is_string() || request.at("anchor").is_string())
                item = resolve(r, request.at("label").is_string() ? str(request, "label")
                                                                  : str(request, "anchor"));
            else
                item = invoke(r, "view", request);
        } catch (const std::exception &e) {
            item = O{{"error", e.what()}};
        }
        item["request_index"] = index;
        auto &items = result["items"].as_array_mut();
        items.push_back(item);
        if (result.dump().size() > size_t(budget)) {
            auto original = str(items.back(), "text");
            if (!original.empty()) {
                items.back()["truncated"] = true;
                size_t lo = 0, hi = original.size();
                while (lo < hi) {
                    size_t mid = (lo + hi + 1) / 2;
                    items.back()["text"] = original.substr(0, mid);
                    if (result.dump().size() <= size_t(budget) - 32)
                        lo = mid;
                    else
                        hi = mid - 1;
                }
                while (lo && (static_cast<unsigned char>(original[lo]) & 0xc0) == 0x80)
                    --lo;
                items.back()["text"] = original.substr(0, lo);
            }
            if (result.dump().size() > size_t(budget) - 32)
                items.pop_back();
            result["truncated"] = true;
            result["omitted"] = requests.as_array().size() - items.size();
            break;
        }
        ++index;
    }
    return result;
}

V state(Rpc *r, const std::string &path) {
    check(agent_reconcile(r, path.c_str()));
    J *out = nullptr;
    check(store_state(r->store, path.c_str(), &out));
    auto result = decode(out);
    json_free(out);
    return result;
}
struct Planned {
    V before, after;
};
V preview(const std::string &id, const V &plan) {
    A diff;
    for (auto &entry : plan.at("entries").as_array()) {
        auto before = str(entry.at("before"), "text"), after = str(entry.at("after"), "text");
        size_t pre = 0, suffix = 0;
        while (pre < before.size() && pre < after.size() && before[pre] == after[pre])
            ++pre;
        while (pre && pre < before.size() && (static_cast<unsigned char>(before[pre]) & 0xc0) == 0x80)
            --pre;
        while (suffix < before.size() - pre && suffix < after.size() - pre &&
               before[before.size() - suffix - 1] == after[after.size() - suffix - 1])
            ++suffix;
        while (suffix && before.size() - suffix < before.size() &&
               (static_cast<unsigned char>(before[before.size() - suffix]) & 0xc0) == 0x80)
            --suffix;
        diff.push_back(O{{"path", entry.at("path")},
                         {"before_exists", entry.at("before").at("exists")},
                         {"after_exists", entry.at("after").at("exists")},
                         {"directory", entry.at("after").at("directory")},
                         {"start", utf8_cp_count(before.data(), pre)},
                         {"removed", before.substr(pre, before.size() - pre - suffix)},
                         {"added", after.substr(pre, after.size() - pre - suffix)}});
    }
    return O{{"plan", id},
             {"base_rev", plan.at("base_rev")},
             {"diff", std::move(diff)},
             {"paths", plan.at("entries").as_array().size()},
             {"applied", false}};
}
V prepare(Rpc *r, const V &p) {
    if (p.at("plan").is_string()) {
        auto id = str(p, "plan"), key = "plan/" + id;
        auto saved = meta(r, key);
        if (saved.is_null())
            throw std::runtime_error("Unknown plan");
        return preview(id, saved);
    }

    if (r->read_only)
        throw std::runtime_error("read_only: transactions are not permitted");
    std::map<std::string, Planned> docs;
    std::map<std::string, std::vector<V>> changes;
    auto get = [&](const std::string &name) -> Planned & {
        auto path = canon(r, name);
        for (auto &[old, value] : docs)
            if (path_equal(old.c_str(), path.c_str()))
                return value;
        auto initial = state(r, path);
        return docs.emplace(path, Planned{initial, initial}).first->second;
    };
    auto parent = [&](const std::string &path) {
        size_t pos = 0;
        while ((pos = path.find('/', pos)) != std::string::npos) {
            auto prefix = path.substr(0, pos++);
            auto &doc = get(prefix);
            if (doc.after.at("exists").as_bool() && !doc.after.at("directory").as_bool())
                throw std::runtime_error("Parent is a file: " + prefix);
            doc.after = O{{"exists", true}, {"directory", true}, {"text", ""}};
        }
    };
    auto &edits = p.at("edits");
    if (!edits.is_null() && !edits.is_array())
        throw std::runtime_error("edits must be an array");
    for (auto edit : edits.as_array()) {
        if (!edit.at("new").is_string())
            throw std::runtime_error("new text is required");
        auto replacement = str(edit, "new"); // Empty replacement is valid for node deletion.
        if (edit.at("new").is_string())
            replacement = str(edit, "new");
        std::string path;
        size_t from = 0, to = 0;
        V node;
        if (edit.at("anchor").is_string() || edit.at("label").is_string()) {
            node = resolve(r, edit.at("label").is_string() ? str(edit, "label") : str(edit, "anchor"));
            path = str(node, "path");
            from = node.at("start").as_uint64();
            to = node.at("end").as_uint64();
        } else {
            path = canon(r, required(edit, "target"));
            auto &doc = get(path);
            auto text = str(doc.before, "text");
            if (edit.at("symbol").is_string()) {
                auto selected = invoke(r, "select",
                                       O{{"target", path},
                                         {"name", edit.at("symbol")},
                                         {"scope", edit.at("scope")},
                                         {"kind", edit.at("kind")}});
                from = selected.at("start").as_uint64();
                to = selected.at("end").as_uint64();
            } else if (edit.at("old").is_string()) {
                auto old = required(edit, "old");
                auto pos = text.find(old);
                if (pos == std::string::npos || text.find(old, pos + 1) != std::string::npos)
                    throw std::runtime_error("old text must match exactly once");
                from = utf8_cp_count(text.data(), pos);
                to = from + utf8_cp_count(old.data(), old.size());
            } else if (edit.at("start").is_number() && edit.at("end").is_number()) {
                from = edit.at("start").as_uint64();
                to = edit.at("end").as_uint64();
                if (!edit.at("expected_hash").is_string() || fingerprint(text) != str(edit, "expected_hash"))
                    throw std::runtime_error("Raw offsets require expected_hash of the source");
            } else
                throw std::runtime_error("Edit needs anchor, label, symbol, old text, or checked offsets");
        }
        auto &doc = get(path);
        if (!doc.before.at("exists").as_bool() || doc.before.at("directory").as_bool())
            throw std::runtime_error("Edit target is not a file");
        auto position = str(edit, "position");
        if (position == "before")
            to = from;
        else if (position == "after")
            from = to;
        else if (!position.empty() && position != "replace")
            throw std::runtime_error("position must be replace, before or after");
        auto source = str(doc.before, "text");
        bool crlf = source.find("\r\n") != std::string::npos, bare = false;
        for (size_t j = 0; j < source.size(); j++)
            if (source[j] == '\n' && (j == 0 || source[j - 1] != '\r'))
                bare = true;
        if (crlf && !bare) {
            std::string adapted;
            for (size_t j = 0; j < replacement.size(); j++) {
                if (replacement[j] == '\n' && (j == 0 || replacement[j - 1] != '\r'))
                    adapted += '\r';
                adapted += replacement[j];
            }
            replacement = std::move(adapted);
        }
        for (auto &[known, value] : docs)
            if (path_equal(known.c_str(), path.c_str())) {
                path = known;
                break;
            }
        changes[path].push_back(O{{"start", from}, {"end", to}, {"text", replacement}});
    }
    for (auto &[path, edits] : changes) {
        auto &doc = get(path);
        auto source = str(doc.before, "text");
        ChangeSet cs{};
        cs_init(&cs);
        for (auto &edit : edits)
            cs_add(&cs, edit.at("start").as_uint64(), edit.at("end").as_uint64(), str(edit, "text").data(),
                   str(edit, "text").size());
        cs_sort(&cs);
        char *after = nullptr;
        size_t len = 0;
        int rc = cs_validate(&cs);
        if (!rc)
            rc = cs_apply_text(&cs, source.data(), source.size(), &after, &len);
        cs_free(&cs);
        check(rc);
        doc.after["text"] = std::string(after, len);
        free(after);
    }
    std::function<void(const std::string &, const std::string &, bool)> move;
    move = [&](const std::string &src, const std::string &dst, bool recursive) {
        auto source = get(src).after;
        if (!source.at("exists").as_bool())
            throw std::runtime_error("Missing source: " + src);
        if (!dst.empty() && get(dst).after.at("exists").as_bool())
            throw std::runtime_error("Destination exists: " + dst);
        bool dir = source.at("directory").as_bool();
        if (dir) {
            StrList files{}, dirs{};
            sl_init(&files);
            sl_init(&dirs);
            auto absolute = std::string(store_root(r->store)) + "/" + src;
            int rc=pf_is_dir(absolute.c_str())?pf_list_dir(absolute.c_str(),&files,&dirs):0;
            if(rc){sl_free(&files);sl_free(&dirs);check(rc);}
            std::set<std::string> names_set;
            for(size_t j=0;j<files.n;j++)names_set.insert(files.v[j]);for(size_t j=0;j<dirs.n;j++)names_set.insert(dirs.v[j]);sl_free(&files);sl_free(&dirs);
            auto add_child=[&](const std::string& path){if(path.size()>src.size()&&path[src.size()]=='/'&&path_equal(path.substr(0,src.size()).c_str(),src.c_str())){auto rest=path.substr(src.size()+1);names_set.insert(rest.substr(0,rest.find('/')));}};
            for(auto& [path,doc]:docs)add_child(path);
            StrList journal{};sl_init(&journal);check(store_state_paths(r->store,&journal));for(size_t j=0;j<journal.n;j++)add_child(journal.v[j]);sl_free(&journal);
            std::vector<std::string> names;for(auto& child:names_set)if(get(src+"/"+child).after.at("exists").as_bool())names.push_back(child);
            if(!names.empty()&&!recursive)throw std::runtime_error("Directory operation requires recursive:true");
            if (!dst.empty()) {
                parent(dst);
                get(dst).after = O{{"exists", true}, {"directory", true}, {"text", ""}};
            }
            for (auto &child : names) {
                if (docs.size() > 2000)
                    throw std::runtime_error("Transaction file limit exceeded");
                move(canon(r, src + "/" + child), dst.empty() ? "" : canon(r, dst + "/" + child), true);
            }
        } else if (!dst.empty()) {
            parent(dst);
            get(dst).after = source;
        }
        get(src).after = O{{"exists", false}, {"directory", dir}, {"text", ""}};
    };
    auto &files = p.at("files");
    if (!files.is_null() && !files.is_array())
        throw std::runtime_error("files must be an array");
    for (auto &op : files.as_array()) {
        auto path = canon(r, required(op, "path")), action = required(op, "op");
        if (action == "create" || action == "mkdir") {
            auto &doc = get(path);
            if (doc.after.at("exists").as_bool())
                throw std::runtime_error("Path exists: " + path);
            parent(path);
            doc.after = O{{"exists", true},
                          {"directory", action == "mkdir"},
                          {"text", action == "mkdir" ? "" : str(op, "text")}};
        } else if (action == "delete")
            move(path, "", op.at("recursive").as_bool());
        else if (action == "move") {
            auto to = canon(r, required(op, "to"));
            if (path_equal(path.c_str(), to.c_str()) ||
                (to.size() > path.size() && to[path.size()] == '/' &&
                 path_equal(to.substr(0, path.size()).c_str(), path.c_str())))
                throw std::runtime_error("Invalid move destination");
            move(path, to, op.at("recursive").as_bool());
        } else
            throw std::runtime_error("File operation must be create, mkdir, move or delete");
    }
    A entries, diff;
    size_t bytes = 0;
    for (auto &[path, doc] : docs) {
        if (doc.before.dump() == doc.after.dump())
            continue;
        entries.push_back(O{{"path", path}, {"before", doc.before}, {"after", doc.after}});
        bytes += str(doc.before, "text").size() + str(doc.after, "text").size();
        diff.push_back(O{{"path", path},
                         {"before_exists", doc.before.at("exists")},
                         {"after_exists", doc.after.at("exists")},
                         {"directory", doc.after.at("directory")},
                         {"removed", doc.before.at("text")},
                         {"added", doc.after.at("text")}});
    }
    if (entries.empty() || entries.size() > 2000 || bytes > 8 * 1024 * 1024)
        throw std::runtime_error("Transaction must change 1..2000 paths and at most 8 MiB");
    V plan = O{{"base_rev", store_head(r->store)}, {"entries", entries}, {"label", str(p, "label")}};
    auto id = "plan_" + fingerprint(plan.dump());
    auto existing = meta(r, "plan/" + id);
    if (existing.is_null())
        put(r, "plan/" + id, plan, "missing");
    return preview(id, plan);
}
V commit(Rpc *r, const V &p) {
    if (r->read_only)
        throw std::runtime_error("read_only: commit not permitted");
    auto id = required(p, "plan");
    auto plan = meta(r, "plan/" + id);
    if (plan.is_null())
        throw std::runtime_error("Unknown prepared plan");
    auto completed = meta(r, "committed/" + id);
    EditResult result{};
    if (completed.is_null()) {
        std::unique_ptr<J, decltype(&json_free)> entries(encode(plan.at("entries")), json_free);
        auto label = str(plan, "label");
        check(store_transaction(r->store, entries.get(),
                                p.at("if_rev").is_null() ? STORE_NO_REV : p.at("if_rev").as_int(),
                                label.empty() ? "agent transaction" : label.c_str(), r->session, id.c_str(),
                                r->has_validator ? &r->validator : nullptr, &result));
    } else {
        result.rev = completed.at("rev").as_int();
        sl_init(&result.paths);
        for (auto &entry : plan.at("entries").as_array())
            sl_push(&result.paths, str(entry, "path").c_str());
    }
    V response = O{{"plan", id},
                   {"rev", result.rev},
                   {"committed", true},
                   {"already_committed", !completed.is_null()},
                   {"current_revision", store_head(r->store)},
                   {"paths", A{}}};
    for (size_t i = 0; i < result.paths.n; i++)
        response["paths"].as_array_mut().push_back(result.paths.v[i]);
    if (r->autoflush) {
        std::vector<const char *> paths;
        for (size_t i = 0; i < result.paths.n; i++)
            paths.push_back(result.paths.v[i]);
        StrList written{};
        sl_init(&written);
        int rc = store_flush(r->store, paths.data(), paths.size(), &written);
        response["written"] = rc == 0;
        response["written_paths"] = A{};
        for (size_t i = 0; i < written.n; i++)
            response["written_paths"].as_array_mut().push_back(written.v[i]);
        if (rc)
            response["flush_error"] = na_errmsg();
        sl_free(&written);
    } else
        response["written"] = false;
    edit_result_free(&result);
    return response;
}
V warm(Rpc *r, const V &p) {
    if (!p.at("targets").is_array() || p.at("targets").as_array().size() > 128)
        throw std::runtime_error("targets must be an array of at most 128 paths");
    A reports;
    Engine engine(r);
    for (auto &item : p.at("targets").as_array()) {
        auto path = canon(r, std::string(item.as_string()));
        auto snap = current(r, path);
        auto abs = std::string(store_root(r->store)) + "/" + path;
        auto started = pf_now_ms();
        if (language_sync(engine.value, abs.c_str(), snap->text, snap->len))
            reports.push_back(O{{"path", path}, {"error", na_errmsg()}});
        else
            reports.push_back(O{{"path", path}, {"rev", snap->rev}, {"parse_ms", pf_now_ms() - started}});
    }
    return O{{"files", std::move(reports)}};
}
} // namespace
int agent_reconcile(Rpc *rpc, const char *path) {
    J *report = nullptr;
    if (store_merge_external(rpc->store, path, "auto", rpc->read_only, &report)) {
        json_free(report);
        return -1;
    }
    auto status = j_get(report, "status");
    bool conflict = j_is_str(status) &&
                    (!strcmp(status->s, "conflict") || (rpc->read_only && strcmp(status->s, "unchanged")));
    json_free(report);
    if (conflict)
        return na_fail("external_conflict: reconcile %s before continuing", path);
    return 0;
}
int agent_method(Rpc *rpc, const char *method, const J *params, J **out) {
    try {
        auto p = decode(params);
        V result;
        if (!strcmp(method, "view") && (p.at("label").is_string() || p.at("anchor").is_string()))
            result = resolve(rpc, p.at("label").is_string() ? str(p, "label") : str(p, "anchor"));
        else if(!strcmp(method,"view")&&p.at("with_anchor").as_bool())result=anchored_read(rpc,p);
        else if (!strcmp(method, "capabilities")) {
            Engine engine(rpc);
            J *mods = language_status(engine.value);
            result = O{{"version",NA_VERSION},{"modules", decode(mods)},
                       {"labels", "AST content anchors; changed/ambiguous nodes are rejected"},
                       {"transactions", "Atomic journal commit; recoverable per-path disk flush"}};
            json_free(mods);
        } else if (!strcmp(method, "check_status")) {
            auto id = str(p, "check_id");
            auto saved = meta(rpc, id.empty() ? "check/latest" : "check/" + id);
            if (saved.is_null())
                throw std::runtime_error("No recorded check");
            std::unique_ptr<J, decltype(&json_free)> paths(encode(saved.at("input_paths")), json_free);
            J *proof = nullptr;
            check(check_fingerprint(rpc->store, paths.get(), &proof));
            auto now = decode(proof);
            json_free(proof);
            bool fresh = saved.at("checked_revision").as_int() == store_head(rpc->store) &&
                         now.at("complete").as_bool() && saved.at("inputs").at("complete").as_bool() &&
                         str(now, "hash") == str(saved.at("inputs"), "hash");
            saved["fresh"] = fresh;
            saved["current_revision"] = store_head(rpc->store);
            saved["current_inputs"] = now;
            result = saved;
        } else if (!strcmp(method, "change")) {
            auto prepared = prepare(rpc, p);
            result = commit(rpc, O{{"plan", prepared.at("plan")}});
        } else if (!strcmp(method, "labels"))
            result = labels(rpc, p);
        else if (!strcmp(method, "read_batch"))
            result = read_batch(rpc, p);
        else if (!strcmp(method, "warm"))
            result = warm(rpc, p);
        else if (!strcmp(method, "prepare"))
            result = prepare(rpc, p);
        else if (!strcmp(method, "commit"))
            result = commit(rpc, p);
        else
            return -2;
        *out = encode(result);
        return 0;
    } catch (const std::exception &e) {
        return na_fail("%s", e.what());
    }
}
