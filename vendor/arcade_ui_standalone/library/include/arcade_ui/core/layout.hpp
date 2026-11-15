// arcade_ui/core/layout.hpp
//
// 2-pass Measure and Arrange layout engine operating over flat slot ranges (§8).
#pragma once

#include <span>
#include <vector>
#include "types.hpp"
#include "ir.hpp"
#include "storage.hpp"

namespace arcade_ui {

class LayoutEngine {
public:
    // Execute a flat sequence of layout operations for an instance
    static void ExecuteLayout(UiStorage& storage, InstanceId instance,
                              std::span<const LayoutOpDesc> descriptors,
                              std::span<const LayoutOp> ops,
                              BoxConstraints root_constraints);

private:
    static Vec2 MeasureOp(UiStorage& storage, InstanceId instance,
                          const LayoutOpDesc& desc, const LayoutOp& op,
                          BoxConstraints constraints);

    static void ArrangeOp(UiStorage& storage, InstanceId instance,
                          const LayoutOpDesc& desc, const LayoutOp& op,
                          Rect bounds);
};

} // namespace arcade_ui
