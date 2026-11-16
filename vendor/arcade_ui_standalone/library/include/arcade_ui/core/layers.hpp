// arcade_ui/core/layers.hpp
//
// LayerStack: visual ordering, modality, input clipping, and focus scopes (§7).
#pragma once

#include <vector>
#include <optional>
#include <span>
#include <algorithm>
#include "types.hpp"
#include "ir.hpp"

namespace arcade_ui {

struct LayerDesc {
    LayerId id;
    LayerBand band = LayerBand::Content;
    uint64_t order_token = 0;
    InputPolicy input_policy = InputPolicy::Interactive;
    FocusScopeId focus_scope;
    Range command_segments;
    Range input_segments;
    Rect viewport;
    bool visible = true;

    // Stable sort key for rendering: lower draws first, higher draws on top
    constexpr uint64_t SortKey() const noexcept {
        return (static_cast<uint64_t>(band) << 48) | (order_token & 0x0000FFFFFFFFFFFFULL);
    }
};

class LayerStack {
public:
    LayerStack() = default;

    LayerId PushLayer(LayerBand band, InputPolicy policy = InputPolicy::Interactive,
                      FocusScopeId focus_scope = {}) {
        const uint32_t index = static_cast<uint32_t>(layers_.size());
        const uint32_t gen = next_generation_++;
        const LayerId id{index, gen};

        LayerDesc desc{};
        desc.id = id;
        desc.band = band;
        desc.order_token = next_order_token_++;
        desc.input_policy = policy;
        desc.focus_scope = focus_scope;
        desc.visible = true;

        layers_.push_back(desc);
        RebuildOrder();
        return id;
    }

    bool RemoveLayer(LayerId id) {
        auto it = std::find_if(layers_.begin(), layers_.end(),
                               [&](const LayerDesc& l) { return l.id == id; });
        if (it == layers_.end()) return false;
        layers_.erase(it);
        RebuildOrder();
        return true;
    }

    bool RaiseLayer(LayerId id) {
        auto* layer = FindLayer(id);
        if (!layer) return false;
        layer->order_token = next_order_token_++;
        RebuildOrder();
        return true;
    }

    bool SetVisible(LayerId id, bool visible) {
        auto* layer = FindLayer(id);
        if (!layer) return false;
        if (layer->visible != visible) {
            layer->visible = visible;
            RebuildOrder();
        }
        return true;
    }

    LayerDesc* FindLayer(LayerId id) noexcept {
        for (auto& l : layers_) {
            if (l.id == id) return &l;
        }
        return nullptr;
    }

    const LayerDesc* FindLayer(LayerId id) const noexcept {
        for (const auto& l : layers_) {
            if (l.id == id) return &l;
        }
        return nullptr;
    }

    // Sorted layers from bottom to top for rendering (painting)
    std::span<const LayerDesc* const> PaintOrder() const noexcept {
        return paint_order_;
    }

    // Top-to-bottom layers for hit-testing
    std::span<const LayerDesc* const> HitTestOrder() const noexcept {
        return hit_test_order_;
    }

    size_t Size() const noexcept { return layers_.size(); }

private:
    void RebuildOrder() {
        paint_order_.clear();
        paint_order_.reserve(layers_.size());
        for (const auto& l : layers_) {
            if (l.visible) {
                paint_order_.push_back(&l);
            }
        }
        // Ascending sort by band and token for painting (bottom-to-top)
        std::sort(paint_order_.begin(), paint_order_.end(),
                  [](const LayerDesc* a, const LayerDesc* b) {
                      return a->SortKey() < b->SortKey();
                  });

        // Descending for hit testing (top-to-bottom)
        hit_test_order_ = paint_order_;
        std::reverse(hit_test_order_.begin(), hit_test_order_.end());
    }

    std::vector<LayerDesc> layers_;
    std::vector<const LayerDesc*> paint_order_;
    std::vector<const LayerDesc*> hit_test_order_;
    uint64_t next_order_token_ = 1;
    uint32_t next_generation_ = 1;
};

} // namespace arcade_ui
