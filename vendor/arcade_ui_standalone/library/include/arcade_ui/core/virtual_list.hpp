// arcade_ui/core/virtual_list.hpp
//
// Virtual list engine for huge collections (§10.1).
// Computes active slice [first, end) based on scroll offset, item height, and viewport height.
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <span>
#include "types.hpp"

namespace arcade_ui {

struct VirtualListSlice {
    uint32_t first = 0;
    uint32_t end = 0;
    float start_y = 0.0f;
    float total_content_height = 0.0f;

    [[nodiscard]] uint32_t Count() const noexcept { return end - first; }
    [[nodiscard]] bool Empty() const noexcept { return first >= end; }
};

class VirtualListEngine {
public:
    // Fixed-height list calculation (§10.1)
    static VirtualListSlice ComputeSlice(uint32_t total_items,
                                         float item_height,
                                         float scroll_y,
                                         float viewport_height,
                                         uint32_t overscan = 2) {
        VirtualListSlice slice{};
        if (total_items == 0 || item_height <= 0.0f) {
            return slice;
        }

        slice.total_content_height = static_cast<float>(total_items) * item_height;

        // Clamp scroll_y to valid range [0, max(0, total_content_height - viewport_height)]
        const float max_scroll = (std::max)(0.0f, slice.total_content_height - viewport_height);
        const float clamped_y = (std::clamp)(scroll_y, 0.0f, max_scroll);

        const int64_t raw_first = static_cast<int64_t>(std::floor(clamped_y / item_height)) - static_cast<int64_t>(overscan);
        const int64_t raw_end = static_cast<int64_t>(std::ceil((clamped_y + viewport_height) / item_height)) + static_cast<int64_t>(overscan);

        slice.first = static_cast<uint32_t>((std::clamp)(raw_first, int64_t{0}, static_cast<int64_t>(total_items)));
        slice.end = static_cast<uint32_t>((std::clamp)(raw_end, static_cast<int64_t>(slice.first), static_cast<int64_t>(total_items)));
        slice.start_y = static_cast<float>(slice.first) * item_height;

        return slice;
    }
};

} // namespace arcade_ui
