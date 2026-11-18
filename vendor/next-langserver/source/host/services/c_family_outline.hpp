#pragma once
#include <algorithm>
// Declaration structure comes from the native parser tree, never a second lexer.
namespace lx::host {
inline std::vector<DocumentSymbolNode> c_family_outline(const Snapshot &snapshot) {
    using Node = lx::uast::GreenNode;
    struct Located {
        const Node *node = nullptr;
        uint32_t off = 0;
    };
    auto mod = snapshot.module.get();
    auto name = [&](const Node *n) -> std::string_view {
        return n && !n->is_seq() ? mod->symbol_name(lx::uast::kind_symbol(n->kind)) : std::string_view{};
    };
    auto children = [&](Located p, auto fn) {
        if (p.node && p.node->is_inner()) {
            auto off = p.off;
            for (auto &c : p.node->as_inner()->children()) {
                if (c.node) {
                    fn(Located{c.node, off});
                    off += c.node->width;
                }
            }
        }
    };
    std::function<Located(Located, std::string_view)> first = [&](Located p,
                                                                  std::string_view rule) -> Located {
        if (!p.node)
            return {};
        if (name(p.node) == rule)
            return p;
        Located result;
        children(p, [&](Located c) {
            if (!result.node)
                result = first(c, rule);
        });
        return result;
    };
    std::function<Located(Located)> identifier = [&](Located p) -> Located {
        if (!p.node)
            return {};
        if (p.node->is_token())
            return name(p.node) == "Identifier" ? p : Located{};
        if (name(p.node) == "parameterDeclarationClause" || name(p.node) == "parameterTypeList" ||
            name(p.node) == "parameterList")
            return {};
        Located result;
        children(p, [&](Located c) {
            if (!result.node)
                result = identifier(c);
        });
        return result;
    };
    std::function<void(Located, std::vector<Located> &)> declarators = [&](Located p,
                                                                           std::vector<Located> &out) {
        auto n = name(p.node);
        if (n == "declarator") {
            out.push_back(p);
            return;
        }
        if (n == "structOrUnionSpecifier" || n == "classSpecifier" || n == "enumSpecifier" ||
            n == "compoundStatement")
            return;
        children(p, [&](Located c) { declarators(c, out); });
    };
    auto emit = [&](std::vector<DocumentSymbolNode> &out, Located whole, Located id, int kind) {
        if (!id.node || !whole.node)
            return;
        DocumentSymbolNode s;
        s.name = snapshot.rope.slice_text(id.off, id.off + id.node->width);
        s.kind = kind;
        s.range = {whole.off, whole.off + whole.node->width};
        s.selection_range = {id.off, id.off + id.node->width};
        out.push_back(std::move(s));
    };
    std::function<void(Located, std::vector<DocumentSymbolNode> &, bool)> walk;
    walk = [&](Located p, std::vector<DocumentSymbolNode> &out, bool field) {
        if (!p.node || p.node->is_token() || p.node->is_error() || p.node->is_missing())
            return;
        const auto rule = name(p.node);
        if (rule == "functionDefinition") {
            auto d = first(p, "declarator");
            emit(out, p, identifier(d), field ? 6 : 12);
            return;
        }
        if (rule == "structOrUnionSpecifier" || rule == "classSpecifier" || rule == "enumSpecifier") {
            Located id;
            children(p, [&](Located c) {
                auto n = name(c.node);
                if (!id.node && (n == "Identifier" || n == "classHead" || n == "enumHead")) {
                    id = n == "Identifier" ? c : identifier(c);
                }
            });
            int kind = rule == "enumSpecifier" ? 10 : 23;
            if (id.node) {
                emit(out, p, id, kind);
                auto &symbol = out.back();
                children(p, [&](Located c) { walk(c, symbol.children, true); });
            } else
                children(p, [&](Located c) { walk(c, out, true); });
            return;
        }
        if (rule == "enumerator") {
            emit(out, p, identifier(p), 22);
            return;
        }
        if (rule == "declaration" || rule == "simpleDeclaration" || rule == "memberDeclaration" ||
            rule == "structDeclaration") {
            // C++ declaration is a wrapper around function/name/template declarations.
            if (rule == "declaration" && mod->id == "cpp") {
                children(p, [&](Located c) { walk(c, out, field); });
                return;
            }
            std::vector<DocumentSymbolNode> tags;
            std::function<void(Located)> find_tags = [&](Located q) {
                auto n = name(q.node);
                if (n == "structOrUnionSpecifier" || n == "classSpecifier" || n == "enumSpecifier") {
                    walk(q, tags, field);
                    return;
                }
                if (n == "declarator" || n == "initializer")
                    return;
                children(q, find_tags);
            };
            children(p, find_tags);
            std::vector<Located> ds;
            declarators(p, ds);
            auto text = snapshot.rope.slice_text(p.off, p.off + p.node->width);
            bool alias = first(p, "storageClassSpecifier").node && text.find("typedef") != std::string::npos;
            for (auto d : ds) {
                auto id = identifier(d);
                if (!id.node)
                    continue;
                auto text_id = snapshot.rope.slice_text(id.off, id.off + id.node->width);
                bool duplicate = false;
                for (auto &t : tags)
                    if (t.name == text_id) {
                        duplicate = true;
                        t.range = {p.off, p.off + p.node->width};
                    }
                if (!duplicate) {
                    bool fn = first(d, "parameterTypeList").node ||
                              first(d, "parametersAndQualifiers").node || first(d, "parameterList").node;
                    emit(out, p, id, alias ? 26 : field ? 8 : fn ? 12 : 13);
                    if(fn && !field && !alias) out.back().detail="declaration";
                }
            }
            for (auto &t : tags)
                out.push_back(std::move(t));
            return;
        }
        children(p, [&](Located c) { walk(c, out, field); });
    };
    std::vector<DocumentSymbolNode> out;
    walk({snapshot.root.get(), 0}, out, false);
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        return a.selection_range.start < b.selection_range.start;
    });
    return out;
}
} // namespace lx::host
