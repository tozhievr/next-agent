#include "host/layer_manager.hpp"

#include "host/extensions.hpp"
#include "host/query_engine.hpp"
#include "host/query_parser.hpp"
#include "lx/lx_constants.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

namespace lx::host {

namespace {

std::string get_inject_query(const LoadedModule* mod) {
  if (!mod) return {};
  if (const void* raw = mod->safe_get_extension("lx.inject.v1")) {
    const auto* ext = static_cast<const lx_ext_inject_v1*>(raw);
    if (ext->size >= sizeof(lx_ext_inject_v1) && ext->query_scm.ptr && ext->query_scm.len > 0) {
      return std::string(ext->query_scm.ptr, ext->query_scm.len);
    }
  }
  return mod->manifest.inject_query;
}

const lx_ext_inject_v1* get_inject_ext(const LoadedModule* mod) {
  if (!mod) return nullptr;
  const void* raw = mod->safe_get_extension("lx.inject.v1");
  if (!raw) return nullptr;
  const auto* ext = static_cast<const lx_ext_inject_v1*>(raw);
  if (ext->size < sizeof(lx_ext_inject_v1)) return nullptr;
  return ext;
}

lx_range view_to_doc_range(const lx::RopeView* view, uint32_t s, uint32_t e) {
  if (!view) return lx_range{s, e};
  uint32_t ds = view->view_to_doc(s, lx::Assoc::After);
  uint32_t de = (e > s) ? view->view_to_doc(e, lx::Assoc::Before) : ds;
  if (de < ds) de = ds;
  return lx_range{ds, de};
}

lx_range trim_quotes_range(const lx::Rope& rope, lx_range r) {
  if (r.end - r.start < 2) return r;
  uint8_t first = rope.byte_at(r.start);
  uint8_t last = rope.byte_at(r.end - 1);
  if ((first == '"' && last == '"') || (first == '\'' && last == '\'') ||
      (first == '`' && last == '`')) {
    return lx_range{r.start + 1, r.end - 1};
  }
  return r;
}

bool ranges_overlap(lx_range a, lx_range b) {
  return a.start < b.end && b.start < a.end;
}

bool ranges_equal(const std::vector<lx_range>& a, const std::vector<lx_range>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].start != b[i].start || a[i].end != b[i].end) return false;
  }
  return true;
}

bool has_cycle(const std::vector<std::pair<std::string, lx_range>>& ancestors,
               const std::string& lang_id,
               const std::vector<lx_range>& ranges) {
  for (const auto& [anc_lang, anc_range] : ancestors) {
    if (anc_lang == lang_id) {
      for (const auto& r : ranges) {
        if (ranges_overlap(anc_range, r)) return true;
      }
    }
  }
  return false;
}

lx::ChangeSet build_view_changeset(const lx::RopeView& old_view,
                                   const lx::ChangeSet& root_changes) {
  std::vector<lx::Hunk> view_hunks;
  for (const auto& h : root_changes.hunks()) {
    // Check if hunk overlaps any range in old_view
    for (const auto& vr : old_view.ranges()) {
      if (h.old_end < vr.start || h.old_start > vr.end) continue;
      if (h.old_end == vr.start && h.old_start == vr.start) continue;
      uint32_t cs = std::max(h.old_start, vr.start);
      uint32_t ce = std::min(h.old_end, vr.end);
      auto vs = old_view.doc_to_view(cs, lx::Assoc::After);
      auto ve = old_view.doc_to_view(ce, lx::Assoc::Before);
      if (vs && ve) {
        uint32_t s = *vs;
        uint32_t e = std::max(s, *ve);
        // Approximate new_len proportionally if hunk was clipped, exact if unclipped
        uint32_t nl = (cs == h.old_start && ce == h.old_end)
                          ? h.new_len
                          : std::min(h.new_len, e - s);
        view_hunks.push_back(lx::Hunk{s, e, nl});
      }
    }
  }
  return lx::ChangeSet::from_hunks(view_hunks);
}

std::vector<lx_range> map_ranges(const std::vector<lx_range>& old_ranges,
                                 const lx::ChangeSet& cs,
                                 uint32_t max_bytes) {
  std::vector<lx_range> out;
  out.reserve(old_ranges.size());
  for (const auto& r : old_ranges) {
    uint32_t ns = cs.map_pos(r.start, lx::Assoc::After);
    uint32_t ne = cs.map_pos(r.end, lx::Assoc::Before);
    if (ns > max_bytes) ns = max_bytes;
    if (ne > max_bytes) ne = max_bytes;
    if (ne < ns) ne = ns;
    out.push_back(lx_range{ns, ne});
  }
  return out;
}

}  // namespace

std::vector<InjectionSpec> LayerManager::discover_injections(
    const lx::uast::GreenNode* parent_root,
    const lx::Rope& root_rope,
    const lx::RopeView* parent_view,
    const LoadedModule* parent_module,
    std::span<const lx::uast::Range> /*changed_ranges*/) {
  std::vector<InjectionSpec> specs;
  if (!parent_root || !parent_module) return specs;

  // 1. Structured rules from lx.inject.v1 extension
  if (const auto* ext = get_inject_ext(parent_module)) {
    if (ext->rules && ext->rule_count > 0) {
      std::unordered_map<std::string, InjectionSpec> combined_acc;

      std::function<void(const lx::uast::GreenNode*, uint32_t)> walk =
          [&](const lx::uast::GreenNode* node, uint32_t off) {
            if (!node) return;
            lx_symbol sym = lx::uast::kind_symbol(node->kind);
            for (uint32_t i = 0; i < ext->rule_count; ++i) {
              const auto& r = ext->rules[i];
              if (r.node_symbol != sym) continue;
              std::string lang_id(r.language_id.ptr ? r.language_id.ptr : "",
                                  r.language_id.len);
              if (lang_id.empty()) continue;

              uint32_t target_s = off;
              uint32_t target_e = off + node->width;
              const lx::uast::GreenNode* key_node = node;

              if (r.field_id != LX_FIELD_NONE && node->is_inner()) {
                uint32_t cur = off;
                bool found_field = false;
                for (const auto& ref : node->as_inner()->children()) {
                  if (!ref.node) continue;
                  if (ref.field_id == r.field_id) {
                    target_s = cur;
                    target_e = cur + ref.node->width;
                    key_node = ref.node;
                    found_field = true;
                    break;
                  }
                  cur += ref.node->width;
                }
                if (!found_field) continue;
              }

              lx_range doc_r = view_to_doc_range(parent_view, target_s, target_e);
              if (r.trim_quotes) {
                doc_r = trim_quotes_range(root_rope, doc_r);
              }
              if (doc_r.end <= doc_r.start) continue;

              if (r.combined) {
                auto& acc = combined_acc[lang_id];
                acc.language_id = lang_id;
                acc.combined = true;
                acc.key = InjectionKey{nullptr, lang_id, true};
                acc.ranges.push_back(doc_r);
              } else {
                InjectionSpec sp;
                sp.language_id = lang_id;
                sp.combined = false;
                sp.key = InjectionKey{key_node, lang_id, false};
                sp.ranges.push_back(doc_r);
                specs.push_back(std::move(sp));
              }
            }
            if (node->is_inner()) {
              uint32_t cur = off;
              for (const auto& ref : node->as_inner()->children()) {
                if (ref.node) {
                  walk(ref.node, cur);
                  cur += ref.node->width;
                }
              }
            }
          };
      walk(parent_root, 0);

      for (auto& [_, sp] : combined_acc) {
        specs.push_back(std::move(sp));
      }
    }
  }

  // 2. S-expression injection query (@injection.content + @injection.language)
  std::string qsrc = get_inject_query(parent_module);
  if (!qsrc.empty()) {
    auto compiled = compile_sexpr_query(qsrc);
    if (compiled) {
      std::vector<QueryMatch> matches =
          parent_view
              ? QueryEngine::execute(*compiled.query, parent_root, *parent_view, parent_module)
              : QueryEngine::execute(*compiled.query, parent_root, root_rope, parent_module);

      for (const auto& m : matches) {
        const lx::uast::GreenNode* content_node = nullptr;
        lx::uast::Range content_r{};
        std::string lang_id;
        for (const auto& c : m.captures) {
          if (c.capture_name == "injection.content" || c.capture_name == "content") {
            content_node = c.node;
            content_r = c.range;
          } else if (c.capture_name == "injection.language" || c.capture_name == "language") {
            if (parent_view) {
              lang_id = parent_view->slice_text(c.range.start, c.range.end);
            } else {
              lang_id = root_rope.slice_text(c.range.start, c.range.end);
            }
          }
        }
        if (content_node && !content_r.empty() && !lang_id.empty()) {
          lx_range doc_r = view_to_doc_range(parent_view, content_r.start, content_r.end);
          if (doc_r.end > doc_r.start) {
            InjectionSpec sp;
            sp.language_id = lang_id;
            sp.key = InjectionKey{content_node, lang_id, false};
            sp.ranges.push_back(doc_r);
            specs.push_back(std::move(sp));
          }
        }
      }
    }
  }

  return specs;
}

std::vector<std::shared_ptr<const Layer>> LayerManager::update_layers(
    const lx::uast::GreenNode* parent_root,
    const lx::Rope& root_rope,
    const lx::RopeView* parent_view,
    const LoadedModule* parent_module,
    const std::vector<std::shared_ptr<const Layer>>& prev_layers,
    const lx::ChangeSet& root_changes,
    std::span<const lx::uast::Range> changed_ranges,
    LanguageRegistry& registry,
    const ParseBackendFn& parse_fn,
    const HostConfig& config,
    uint32_t depth,
    const std::vector<std::pair<std::string, lx_range>>& ancestor_chain) {
  std::vector<std::shared_ptr<const Layer>> result;
  if (!parent_root || !parent_module || depth > config.max_layer_depth) {
    return result;
  }

  auto specs = discover_injections(parent_root, root_rope, parent_view, parent_module, changed_ranges);
  if (specs.empty()) return result;

  for (const auto& sp : specs) {
    if (sp.ranges.empty()) continue;
    if (has_cycle(ancestor_chain, sp.language_id, sp.ranges)) {
      continue;
    }

    // Match previous layer:
    // 1) Exact (parent_green_ptr, lang, combined) match -> reuse without reparsing (§9.3)
    std::shared_ptr<const Layer> matched_prev;
    bool exact_green_match = false;
    if (sp.key.parent_green != nullptr) {
      for (const auto& prev : prev_layers) {
        if (prev->key == sp.key) {
          matched_prev = prev;
          exact_green_match = true;
          break;
        }
      }
    }

    // 2) Fallback: match by mapped range + language_id (§9.3)
    if (!matched_prev) {
      for (const auto& prev : prev_layers) {
        if (prev->language_id != sp.language_id || prev->key.combined != sp.combined) continue;
        auto mapped = map_ranges(prev->ranges, root_changes, root_rope.byte_length());
        if (ranges_equal(mapped, sp.ranges)) {
          matched_prev = prev;
          break;
        }
        // Overlap match for single-range edits inside an injection
        if (mapped.size() == 1 && sp.ranges.size() == 1 &&
            ranges_overlap(mapped[0], sp.ranges[0])) {
          matched_prev = prev;
          break;
        }
      }
    }

    auto child_mod = registry.get_or_load(sp.language_id);
    if (!child_mod) {
      continue;
    }

    if (exact_green_match && matched_prev &&
        matched_prev->module &&
        matched_prev->module->module_epoch == child_mod->module_epoch &&
        matched_prev->module->info->grammar_hash == child_mod->info->grammar_hash) {
      // Unchanged green pointer: reuse tree without reparsing (§9.3), update RopeView coordinates
      auto reused = std::make_shared<Layer>();
      reused->key = sp.key;
      reused->language_id = sp.language_id;
      reused->ranges = sp.ranges;
      reused->view = root_rope.view(sp.ranges);
      reused->module = matched_prev->module;
      reused->root = matched_prev->root;
      reused->diagnostics = extract_diagnostics(reused->root.get(), reused->module.get());
      reused->depth = depth;
      reused->truncated = matched_prev->truncated;

      auto next_ancestors = ancestor_chain;
      for (const auto& r : sp.ranges) {
        next_ancestors.emplace_back(sp.language_id, r);
      }
      reused->children = update_layers(
          reused->root.get(), root_rope, &reused->view, reused->module.get(),
          matched_prev->children, root_changes, {}, registry, parse_fn, config,
          depth + 1, next_ancestors);
      result.push_back(std::move(reused));
      continue;
    }

    // Reparse layer over its RopeView
    auto layer = std::make_shared<Layer>();
    layer->key = sp.key;
    layer->language_id = sp.language_id;
    layer->ranges = sp.ranges;
    layer->view = root_rope.view(sp.ranges);
    layer->module = child_mod;
    layer->depth = depth;

    lx::ChangeSet view_cs;
    const lx::uast::GreenNode* old_layer_root = nullptr;
    if (matched_prev && matched_prev->root &&
        matched_prev->module &&
        matched_prev->module->module_epoch == child_mod->module_epoch &&
        matched_prev->module->info->grammar_hash == child_mod->info->grammar_hash) {
      old_layer_root = matched_prev->root.get();
      view_cs = build_view_changeset(matched_prev->view, root_changes);
    }

    ParseInput pin;
    pin.module = child_mod.get();
    pin.registry = &registry;
    pin.view = &layer->view;
    pin.old_tree = old_layer_root;
    pin.changes = old_layer_root ? &view_cs : nullptr;
    pin.config = config;
    pin.force_full = (old_layer_root == nullptr);

    ParseOutput pout = parse_fn ? parse_fn(pin) : lex_only_parse_fallback(pin);
    layer->root = std::move(pout.root);
    layer->truncated = pout.truncated;
    layer->diagnostics = extract_diagnostics(layer->root.get(), child_mod.get());
    // Map layer diagnostics to root document byte coordinates
    for (auto& d : layer->diagnostics) {
      uint32_t ds = layer->view.view_to_doc(d.range.start, lx::Assoc::After);
      uint32_t de = layer->view.view_to_doc(d.range.end, lx::Assoc::Before);
      if (de < ds) de = ds;
      d.range = lx_range{ds, de};
    }

    std::vector<lx::uast::Range> layer_diff;
    if (old_layer_root && layer->root) {
      layer_diff = lx::uast::tree_diff(old_layer_root, layer->root.get());
    }

    auto next_ancestors = ancestor_chain;
    for (const auto& r : sp.ranges) {
      next_ancestors.emplace_back(sp.language_id, r);
    }
    const std::vector<std::shared_ptr<const Layer>> empty_prev;
    layer->children = update_layers(
        layer->root.get(), root_rope, &layer->view, child_mod.get(),
        matched_prev ? matched_prev->children : empty_prev,
        root_changes, layer_diff, registry, parse_fn, config,
        depth + 1, next_ancestors);

    result.push_back(std::move(layer));
  }

  return result;
}

// ============================================================================
// LayeredCursor
// ============================================================================

LayeredCursor::LayeredCursor(std::shared_ptr<const Snapshot> snapshot,
                             std::vector<std::shared_ptr<const Layer>> layers)
    : snapshot_(std::move(snapshot)), layers_(std::move(layers)) {
  if (snapshot_ && snapshot_->root) {
    cursor_ = lx::uast::Cursor(snapshot_->root);
  }
}

namespace {

std::shared_ptr<const Layer> find_deepest_layer(
    const std::vector<std::shared_ptr<const Layer>>& layers,
    uint32_t doc_byte) {
  for (const auto& layer : layers) {
    bool inside = false;
    for (const auto& r : layer->ranges) {
      if (doc_byte >= r.start && doc_byte < r.end) {
        inside = true;
        break;
      }
    }
    if (inside) {
      if (auto deeper = find_deepest_layer(layer->children, doc_byte)) {
        return deeper;
      }
      return layer;
    }
  }
  return nullptr;
}

void collect_layer_diags(const std::vector<std::shared_ptr<const Layer>>& layers,
                         std::vector<Diagnostic>& out) {
  for (const auto& l : layers) {
    out.insert(out.end(), l->diagnostics.begin(), l->diagnostics.end());
    collect_layer_diags(l->children, out);
  }
}

void collect_layer_tokens(const std::vector<std::shared_ptr<const Layer>>& layers,
                          std::optional<lx::uast::Range> visible_range,
                          std::vector<SemanticTokenSpan>& out) {
  for (const auto& l : layers) {
    if (l->root) {
      auto spans = Services::highlight_view(l->root.get(), l->view, l->module.get(), std::nullopt);
      for (const auto& s : spans) {
        uint32_t ds = l->view.view_to_doc(s.offset, lx::Assoc::After);
        uint32_t de = l->view.view_to_doc(s.offset + s.length, lx::Assoc::Before);
        if (de <= ds) continue;
        if (visible_range.has_value()) {
          if (de <= visible_range->start || ds >= visible_range->end) continue;
        }
        out.push_back(SemanticTokenSpan{ds, de - ds, s.token_type, s.modifiers});
      }
    }
    collect_layer_tokens(l->children, visible_range, out);
  }
}

}  // namespace

bool LayeredCursor::goto_byte(uint32_t doc_byte) {
  current_layer_ = find_deepest_layer(layers_, doc_byte);
  if (current_layer_ && current_layer_->root) {
    auto voff = current_layer_->view.doc_to_view(doc_byte, lx::Assoc::After);
    if (voff.has_value()) {
      cursor_ = lx::uast::Cursor(current_layer_->root);
      return cursor_.goto_byte(*voff);
    }
  }
  current_layer_.reset();
  if (snapshot_ && snapshot_->root) {
    cursor_ = lx::uast::Cursor(snapshot_->root);
    return cursor_.goto_byte(doc_byte);
  }
  return false;
}

std::string_view LayeredCursor::language_id() const noexcept {
  if (current_layer_) return current_layer_->language_id;
  if (snapshot_ && snapshot_->module) return snapshot_->module->id;
  return {};
}

const LoadedModule* LayeredCursor::module() const noexcept {
  if (current_layer_) return current_layer_->module.get();
  if (snapshot_) return snapshot_->module.get();
  return nullptr;
}

lx::uast::Range LayeredCursor::doc_range() const noexcept {
  lx::uast::Range r = cursor_.range();
  if (!current_layer_) return r;
  uint32_t ds = current_layer_->view.view_to_doc(r.start, lx::Assoc::After);
  uint32_t de = current_layer_->view.view_to_doc(r.end, lx::Assoc::Before);
  if (de < ds) de = ds;
  return lx::uast::Range{ds, de};
}

std::vector<Diagnostic> LayeredCursor::all_diagnostics() const {
  std::vector<Diagnostic> out;
  if (snapshot_) {
    out.insert(out.end(), snapshot_->diagnostics.begin(), snapshot_->diagnostics.end());
  }
  collect_layer_diags(layers_, out);
  std::sort(out.begin(), out.end(), [](const Diagnostic& a, const Diagnostic& b) {
    if (a.range.start != b.range.start) return a.range.start < b.range.start;
    return a.range.end < b.range.end;
  });
  return out;
}

std::vector<SemanticTokenSpan> LayeredCursor::all_semantic_tokens(
    std::optional<lx::uast::Range> visible_range) const {
  std::vector<SemanticTokenSpan> out;
  if (snapshot_) {
    out = Services::highlight(*snapshot_, visible_range);
  }
  std::vector<SemanticTokenSpan> layer_spans;
  collect_layer_tokens(layers_, visible_range, layer_spans);
  if (layer_spans.empty()) return out;

  // Embedded layer tokens take precedence over host tokens covering the same bytes
  std::vector<SemanticTokenSpan> merged;
  merged.reserve(out.size() + layer_spans.size());
  for (const auto& host_s : out) {
    bool covered = false;
    uint32_t hs = host_s.offset;
    uint32_t he = host_s.offset + host_s.length;
    for (const auto& ls : layer_spans) {
      uint32_t l_start = ls.offset;
      uint32_t l_end = ls.offset + ls.length;
      if (hs < l_end && l_start < he) {
        covered = true;
        break;
      }
    }
    if (!covered) merged.push_back(host_s);
  }
  merged.insert(merged.end(), layer_spans.begin(), layer_spans.end());
  std::sort(merged.begin(), merged.end(), [](const SemanticTokenSpan& a, const SemanticTokenSpan& b) {
    if (a.offset != b.offset) return a.offset < b.offset;
    return a.length < b.length;
  });
  return merged;
}

}  // namespace lx::host
