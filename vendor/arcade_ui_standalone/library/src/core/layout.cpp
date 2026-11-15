// arcade_ui/src/core/layout.cpp
#include <arcade_ui/core/layout.hpp>
#include <numeric>

namespace arcade_ui {

void LayoutEngine::ExecuteLayout(UiStorage& storage, InstanceId instance,
                                 std::span<const LayoutOpDesc> descriptors,
                                 std::span<const LayoutOp> ops,
                                 BoxConstraints root_constraints) {
    if (ops.empty()) return;

    // First pass: Measure from leaves up to roots
    // In our linear IR, measure ops precede arrange ops, or appear in dependency order
    for (const auto& op : ops) {
        if (op.opcode == LayoutOpcode::MeasureFixed ||
            op.opcode == LayoutOpcode::MeasureText ||
            op.opcode == LayoutOpcode::MeasureRow ||
            op.opcode == LayoutOpcode::MeasureColumn ||
            op.opcode == LayoutOpcode::MeasureOverlay) {
            const auto& desc = descriptors[op.descriptor_index];
            Vec2 measured = MeasureOp(storage, instance, desc, op, root_constraints);

            // Store measured size in the output slot
            if (!op.outputs.Empty()) {
                Rect& out_rect = storage.GetRect(instance, SlotRef{op.outputs.first});
                out_rect.width = measured.x;
                out_rect.height = measured.y;
            }
        }
    }

    // Second pass: Arrange from roots down to children
    for (const auto& op : ops) {
        if (op.opcode == LayoutOpcode::ArrangeRow ||
            op.opcode == LayoutOpcode::ArrangeColumn ||
            op.opcode == LayoutOpcode::ArrangeOverlay) {
            const auto& desc = descriptors[op.descriptor_index];
            Rect parent_bounds{0.0f, 0.0f, root_constraints.max_width, root_constraints.max_height};
            if (!op.outputs.Empty()) {
                parent_bounds = storage.GetRect(instance, SlotRef{op.outputs.first});
            }
            ArrangeOp(storage, instance, desc, op, parent_bounds);
        }
    }
}

Vec2 LayoutEngine::MeasureOp(UiStorage& storage, InstanceId instance,
                            const LayoutOpDesc& desc, const LayoutOp& op,
                            BoxConstraints constraints) {
    switch (op.opcode) {
        case LayoutOpcode::MeasureFixed: {
            float w = (desc.width_spec.kind == SizeKind::Fixed) ? desc.width_spec.value : 0.0f;
            float h = (desc.height_spec.kind == SizeKind::Fixed) ? desc.height_spec.value : 0.0f;
            return constraints.Constrain(Vec2{w, h});
        }

        case LayoutOpcode::MeasureText: {
            // Text measurement: width and height are taken from fixed or already-measured font slots
            float w = (desc.width_spec.kind == SizeKind::Fixed) ? desc.width_spec.value : 100.0f;
            float h = (desc.height_spec.kind == SizeKind::Fixed) ? desc.height_spec.value : 20.0f;
            return constraints.Constrain(Vec2{w + desc.padding.Horizontal(), h + desc.padding.Vertical()});
        }

        case LayoutOpcode::MeasureColumn: {
            float total_h = desc.padding.Vertical();
            float max_w = 0.0f;
            const uint32_t count = op.inputs.count;

            for (uint32_t i = 0; i < count; ++i) {
                const Rect& child = storage.GetRect(instance, SlotRef{op.inputs.first + i});
                total_h += child.height;
                if (i + 1 < count) total_h += desc.gap;
                max_w = (std::max)(max_w, child.width);
            }
            max_w += desc.padding.Horizontal();
            return constraints.Constrain(Vec2{max_w, total_h});
        }

        case LayoutOpcode::MeasureRow: {
            float total_w = desc.padding.Horizontal();
            float max_h = 0.0f;
            const uint32_t count = op.inputs.count;

            for (uint32_t i = 0; i < count; ++i) {
                const Rect& child = storage.GetRect(instance, SlotRef{op.inputs.first + i});
                total_w += child.width;
                if (i + 1 < count) total_w += desc.gap;
                max_h = (std::max)(max_h, child.height);
            }
            max_h += desc.padding.Vertical();
            return constraints.Constrain(Vec2{total_w, max_h});
        }

        case LayoutOpcode::MeasureOverlay: {
            float max_w = 0.0f;
            float max_h = 0.0f;
            for (uint32_t i = 0; i < op.inputs.count; ++i) {
                const Rect& child = storage.GetRect(instance, SlotRef{op.inputs.first + i});
                max_w = (std::max)(max_w, child.width);
                max_h = (std::max)(max_h, child.height);
            }
            return constraints.Constrain(Vec2{max_w + desc.padding.Horizontal(), max_h + desc.padding.Vertical()});
        }

        default:
            return Vec2{0.0f, 0.0f};
    }
}

void LayoutEngine::ArrangeOp(UiStorage& storage, InstanceId instance,
                            const LayoutOpDesc& desc, const LayoutOp& op,
                            Rect bounds) {
    const uint32_t count = op.inputs.count;
    if (count == 0) return;

    if (op.opcode == LayoutOpcode::ArrangeColumn) {
        float cur_y = bounds.y + desc.padding.top;
        const float available_w = (std::max)(0.0f, bounds.width - desc.padding.Horizontal());

        for (uint32_t i = 0; i < count; ++i) {
            Rect& child = storage.GetRect(instance, SlotRef{op.inputs.first + i});
            float cur_x = bounds.x + desc.padding.left;

            if (desc.align_items == Alignment::Center) {
                cur_x += (available_w - child.width) * 0.5f;
            } else if (desc.align_items == Alignment::End) {
                cur_x += (available_w - child.width);
            } else if (desc.align_items == Alignment::Stretch) {
                child.width = available_w;
            }

            child.x = cur_x;
            child.y = cur_y;
            cur_y += child.height + desc.gap;
        }
    } else if (op.opcode == LayoutOpcode::ArrangeRow) {
        float cur_x = bounds.x + desc.padding.left;
        const float available_h = (std::max)(0.0f, bounds.height - desc.padding.Vertical());

        for (uint32_t i = 0; i < count; ++i) {
            Rect& child = storage.GetRect(instance, SlotRef{op.inputs.first + i});
            float cur_y = bounds.y + desc.padding.top;

            if (desc.align_items == Alignment::Center) {
                cur_y += (available_h - child.height) * 0.5f;
            } else if (desc.align_items == Alignment::End) {
                cur_y += (available_h - child.height);
            } else if (desc.align_items == Alignment::Stretch) {
                child.height = available_h;
            }

            child.x = cur_x;
            child.y = cur_y;
            cur_x += child.width + desc.gap;
        }
    } else if (op.opcode == LayoutOpcode::ArrangeOverlay) {
        const float available_w = (std::max)(0.0f, bounds.width - desc.padding.Horizontal());
        const float available_h = (std::max)(0.0f, bounds.height - desc.padding.Vertical());

        for (uint32_t i = 0; i < count; ++i) {
            Rect& child = storage.GetRect(instance, SlotRef{op.inputs.first + i});
            child.x = bounds.x + desc.padding.left;
            child.y = bounds.y + desc.padding.top;
            if (desc.align_items == Alignment::Stretch) {
                child.width = available_w;
                child.height = available_h;
            }
        }
    }
}

} // namespace arcade_ui
