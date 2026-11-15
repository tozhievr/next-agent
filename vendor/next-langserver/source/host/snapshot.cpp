#include "host/snapshot.hpp"

namespace lx::host {

namespace {

void collect_diagnostics(const lx::uast::GreenNode* node,
                         uint32_t offset,
                         const LoadedModule* mod,
                         const lx::Rope* rope,
                         std::vector<Diagnostic>& out,
                         bool& saw_truncated) {
  if (!node) return;
  uint32_t flags = node->flags;
  constexpr uint32_t kInterested =
      lx::uast::FLAG_ERROR | lx::uast::FLAG_MISSING |
      lx::uast::FLAG_HAS_ERROR | lx::uast::FLAG_TRUNCATED;
  if ((flags & kInterested) == 0) {
    return;
  }

  if (flags & lx::uast::FLAG_TRUNCATED) {
    if (!saw_truncated) {
      saw_truncated = true;
      out.push_back(Diagnostic{
          "LX_E_TRUNCATED",
          "parse truncated by resource limits",
          lx_range{offset, offset + node->width},
          DiagnosticSeverity::Warning,
      });
    }
  }

  if (flags & lx::uast::FLAG_MISSING) {
    lx_symbol sym = lx::uast::kind_symbol(node->kind);
    std::string_view sym_name = mod ? mod->symbol_name(sym) : std::string_view{};
    std::string msg = "missing expected token";
    if (!sym_name.empty()) {
      msg += " '";
      msg += sym_name;
      msg += "'";
    }
    out.push_back(Diagnostic{
        "LX_E_MISSING_TOKEN",
        std::move(msg),
        lx_range{offset, offset + node->width},
        DiagnosticSeverity::Error,
    });
    return;
  }

  if (flags & lx::uast::FLAG_ERROR) {
    if (node->is_token()) {
      std::string msg = "unexpected or invalid token";
      if (rope && node->width > 0 && node->width <= 64) {
        std::string snippet = rope->slice_text(offset, offset + node->width);
        if (!snippet.empty()) {
          msg += " '";
          msg += snippet;
          msg += "'";
        }
      }
      out.push_back(Diagnostic{
          "LX_E_LEX_ERROR",
          std::move(msg),
          lx_range{offset, offset + node->width},
          DiagnosticSeverity::Error,
      });
      return;
    }

    // Rule/Error recovery node
    std::string msg = "unexpected token(s)";
    if (rope && node->width > 0) {
      uint32_t preview_len = node->width < 48 ? node->width : 48;
      std::string snippet = rope->slice_text(offset, offset + preview_len);
      if (!snippet.empty()) {
        msg += " near '";
        msg += snippet;
        if (node->width > preview_len) msg += "...";
        msg += "'";
      }
    }
    out.push_back(Diagnostic{
        "LX_E_UNEXPECTED_TOKEN",
        std::move(msg),
        lx_range{offset, offset + node->width},
        DiagnosticSeverity::Error,
    });
    return;
  }

  if (node->is_inner()) {
    const auto* inner = node->as_inner();
    uint32_t cur = offset;
    for (const auto& ref : inner->children()) {
      if (ref.node) {
        collect_diagnostics(ref.node, cur, mod, rope, out, saw_truncated);
        cur += ref.node->width;
      }
    }
  }
}

}  // namespace

std::vector<Diagnostic> extract_diagnostics(const lx::uast::GreenNode* root,
                                            const LoadedModule* mod,
                                            const lx::Rope* rope,
                                            uint32_t base_offset) {
  std::vector<Diagnostic> out;
  if (!root) return out;
  bool saw_truncated = false;
  collect_diagnostics(root, base_offset, mod, rope, out, saw_truncated);
  return out;
}

}  // namespace lx::host
