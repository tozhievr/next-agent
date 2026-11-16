// arcade_ui/core/dsl.hpp
//
// Declarative React-like C++23 Component DSL (§4).
// Consteval / compile-time descriptor composition into flat ComponentProgram.
// Shadcn-inspired native primitives. This is a C++ DSL, not a React port.
#pragma once

#include <string_view>
#include <tuple>
#include <vector>
#include <type_traits>
#include "types.hpp"
#include "ir.hpp"
#include "storage.hpp"
#include "transaction.hpp"
#include "theme.hpp"
#include "host.hpp"

namespace arcade_ui {

// --- Style descriptor -----------------------------------------------------
struct Style {
    Padding padding;
    float gap = 0.0f;
    Alignment align_items = Alignment::Start;
    Alignment justify_content = Alignment::Start;
    Color background_color = Color::Transparent();
    Color border_color = Color::Transparent();
    float border_width = 0.0f;
    CornerRadii radii;
    SizeSpec width = SizeSpec::Intrinsic();
    SizeSpec height = SizeSpec::Intrinsic();
};

// --- Symbolic Member References (§4.2) ------------------------------------
template <auto MemberPtr>
struct PropRef {};

template <auto MemberPtr>
constexpr auto prop() noexcept { return PropRef<MemberPtr>{}; }

template <auto MemberPtr>
struct StateRef {};

template <auto MemberPtr>
constexpr auto state() noexcept { return StateRef<MemberPtr>{}; }

template <auto MethodPtrVal>
struct OnClickRef {
    static constexpr auto MethodPtr = MethodPtrVal;
};

template <auto MethodPtr>
constexpr auto on_click() noexcept { return OnClickRef<MethodPtr>{}; }

// --- Format helpers --------------------------------------------------------
template <typename T>
struct FormatDecimalExpr {
    T source;
};

template <auto MemberPtr>
constexpr auto format_decimal(StateRef<MemberPtr> ref) noexcept {
    return FormatDecimalExpr<StateRef<MemberPtr>>{ref};
}

// --- DSL Node Descriptors --------------------------------------------------
enum class NodeType {
    Column,
    Row,
    Overlay,
    Text,
    Badge,
    Separator,
    Button,
    Switch,
    ProgressBar,
    Panel
};

template <typename... Children>
struct LayoutNode {
    NodeType type;
    Style style;
    std::tuple<Children...> children;
};

struct TextNode {
    std::string text_literal;
    Color color = Color{0.95f, 0.95f, 0.95f, 1.0f};
    float width = 160.0f;
    float height = 24.0f;
};

enum class BadgeVariant {
    Default,
    Secondary,
    Outline,
    Success,
    Destructive
};

struct BadgeNode {
    std::string label;
    BadgeVariant variant = BadgeVariant::Default;
    float width = 76.0f;
    float height = 22.0f;
};

struct SeparatorNode {
    float width = 240.0f;
    float thickness = 1.0f;
    Color color = theme::Zinc800;
};

enum class ButtonVariant {
    Default,
    Secondary,
    Outline,
    Destructive,
    Ghost,
    Link
};

template <typename HandlerRef>
struct ButtonNode {
    std::string label;
    ButtonVariant variant = ButtonVariant::Default;
    Style style;
    HandlerRef handler;
};

template <typename HandlerRef>
struct SwitchNode {
    bool initial_checked = false;
    HandlerRef handler;
};

struct ProgressBarNode {
    float initial_percent = 50.0f;
    float width = 220.0f;
    float height = 8.0f;
};

// A fixed-size surface with optional caption. Useful for alerts, fields,
// skeletons, avatars, and other non-editable display elements.
struct SurfaceNode {
    std::string label;
    float width;
    float height;
    Color fill;
    Color foreground;
    Color border;
    CornerRadii radii;
};

struct EditableNode {
    std::string initial_value;
    float width = 240.0f;
    float height = 36.0f;
};

template <typename HandlerRef>
struct SliderNode {
    float initial_percent = 50.0f;
    float width = 240.0f;
    HandlerRef handler;
};

template <typename HandlerRef>
struct ActionNode {
    std::string label;
    float width;
    float height;
    Color fill;
    Color foreground;
    Color border;
    CornerRadii radii;
    uint32_t semantic_id;
    HandlerRef handler;
    bool selected = false;
};

// --- DSL Builder Functions -------------------------------------------------
template <typename... Args>
auto column(Style style, Args&&... args) {
    return LayoutNode<std::decay_t<Args>...>{
        NodeType::Column, style, std::make_tuple(std::forward<Args>(args)...)
    };
}

template <typename... Args>
auto column(Args&&... args) {
    return column(Style{}, std::forward<Args>(args)...);
}

template <typename... Args>
auto row(Style style, Args&&... args) {
    return LayoutNode<std::decay_t<Args>...>{
        NodeType::Row, style, std::make_tuple(std::forward<Args>(args)...)
    };
}

template <typename... Args>
auto row(Args&&... args) {
    return row(Style{}, std::forward<Args>(args)...);
}

template <typename... Args>
auto overlay(Style style, Args&&... args) {
    return LayoutNode<std::decay_t<Args>...>{
        NodeType::Overlay, style, std::make_tuple(std::forward<Args>(args)...)
    };
}

template <typename... Args>
auto overlay(Args&&... args) {
    return overlay(Style{}, std::forward<Args>(args)...);
}

inline auto text(std::string_view lit, Color color = Color{0.95f, 0.95f, 0.95f, 1.0f}, float width = 160.0f, float height = 24.0f) {
    return TextNode{std::string(lit), color, width, height};
}

inline auto muted_text(std::string_view lit, float width = 220.0f) {
    return TextNode{std::string(lit), theme::Zinc400, width, 18.0f};
}

inline auto badge(std::string_view label, BadgeVariant variant = BadgeVariant::Default, float width = 76.0f) {
    return BadgeNode{std::string(label), variant, width, 22.0f};
}

inline auto separator(float width = 240.0f, float thickness = 1.0f, Color color = theme::Zinc800) {
    return SeparatorNode{width, thickness, color};
}

template <auto MethodPtr>
inline auto button(std::string_view label, OnClickRef<MethodPtr> handler, ButtonVariant variant = ButtonVariant::Default, Style style = {}) {
    return ButtonNode<OnClickRef<MethodPtr>>{std::string(label), variant, style, handler};
}

template <auto MethodPtr>
inline auto button(std::string_view label, OnClickRef<MethodPtr> handler, Style style) {
    return ButtonNode<OnClickRef<MethodPtr>>{std::string(label), ButtonVariant::Default, style, handler};
}

template <auto MethodPtr>
inline auto switch_toggle(bool initial_checked, OnClickRef<MethodPtr> handler) {
    return SwitchNode<OnClickRef<MethodPtr>>{initial_checked, handler};
}

inline auto progress_bar(float initial_percent = 50.0f, float width = 220.0f, float height = 8.0f) {
    return ProgressBarNode{initial_percent, width, height};
}

inline auto surface(std::string_view label, float width, float height,
                    Color fill = theme::Zinc900, Color foreground = theme::Zinc100,
                    Color border = theme::Zinc700, float radius = 6.0f) {
    return SurfaceNode{std::string(label), width, height, fill, foreground, border, CornerRadii{radius}};
}

inline auto heading(std::string_view label, float width = 260.0f) {
    return text(label, theme::Zinc50, width, 30.0f);
}
inline auto label(std::string_view caption, float width = 220.0f) {
    return text(caption, theme::Zinc200, width, 20.0f);
}
inline auto input_preview(std::string_view value, float width = 240.0f) {
    return surface(value, width, 36.0f, theme::Zinc950, theme::Zinc400);
}
inline auto input(std::string_view initial_value = "", float width = 240.0f) {
    return EditableNode{std::string(initial_value), width, 36.0f};
}
inline auto textarea(std::string_view initial_value = "", float width = 240.0f, float height = 68.0f) {
    return EditableNode{std::string(initial_value), width, height};
}
inline auto textarea_preview(std::string_view value, float width = 240.0f) {
    return surface(value, width, 68.0f, theme::Zinc950, theme::Zinc400);
}
inline auto skeleton(float width, float height = 16.0f) {
    return surface("", width, height, theme::Zinc800, theme::Zinc800, Color::Transparent());
}
inline auto avatar(std::string_view initials, float size = 40.0f) {
    return surface(initials, size, size, theme::Zinc700, theme::Zinc50, Color::Transparent(), size * 0.5f);
}
inline auto alert(std::string_view message, float width = 280.0f) {
    return surface(message, width, 48.0f, theme::Zinc900, theme::Zinc100, theme::Zinc700);
}
inline auto kbd(std::string_view key, float width = 52.0f) {
    return surface(key, width, 24.0f, theme::Zinc800, theme::Zinc200, theme::Zinc700, 4.0f);
}

template <auto MethodPtr>
inline auto action(std::string_view caption, OnClickRef<MethodPtr> handler,
                   float width = 240.0f, float height = 34.0f,
                   Color fill = theme::Zinc800, Color foreground = theme::Zinc100,
                   Color border = theme::Zinc700, uint32_t semantic_id = 1) {
    return ActionNode<OnClickRef<MethodPtr>>{std::string(caption), width, height,
        fill, foreground, border, CornerRadii{6.0f}, semantic_id, handler};
}
template <auto MethodPtr>
inline auto slider(float initial_percent, OnClickRef<MethodPtr> handler, float width = 240.0f) {
    return SliderNode<OnClickRef<MethodPtr>>{initial_percent, width, handler};
}
template <auto MethodPtr>
inline auto checkbox(std::string_view caption, bool checked, OnClickRef<MethodPtr> handler, float width = 240.0f) {
    auto node = action(caption, handler, width, 32.0f,
        Color::Transparent(), theme::Zinc100, Color::Transparent(), 3);
    node.selected = checked;
    return node;
}
template <auto MethodPtr>
inline auto radio_item(std::string_view caption, bool selected, OnClickRef<MethodPtr> handler, float width = 240.0f) {
    auto node = action(caption, handler, width, 32.0f,
        Color::Transparent(), theme::Zinc100, Color::Transparent(), 4);
    node.selected = selected;
    return node;
}
template <auto MethodPtr>
inline auto toggle(std::string_view caption, bool pressed, OnClickRef<MethodPtr> handler, float width = 240.0f) {
    return action(caption, handler, width, 32.0f,
                  pressed ? theme::Blue600 : theme::Zinc800, theme::Zinc100, theme::Zinc700, 5);
}
template <auto MethodPtr>
inline auto tab(std::string_view caption, bool active, OnClickRef<MethodPtr> handler, float width = 120.0f) {
    return action(caption, handler, width, 32.0f,
                  active ? theme::Zinc700 : theme::Zinc900, theme::Zinc100, theme::Zinc700, 6);
}

// --- Component Compiler (§4.2, §5) -----------------------------------------
template <typename Component>
ComponentProgram CompileComponent() {
    if constexpr (requires { Component::BuildProgram(); }) {
        return Component::BuildProgram();
    } else {
    ComponentProgram prog{};
    prog.name = "Component";

    auto desc = Component::Describe();

    uint32_t geom_slot = 0;
    uint32_t style_slot = 0;
    uint32_t input_slot = 0;

    // Root slot
    SlotRef root_geom{geom_slot++};
    SlotRef root_style{style_slot++};

    // Store root container style
    PaintStyleData root_s{};
    root_s.fill_color = desc.style.background_color;
    root_s.border_color = desc.style.border_color;
    root_s.border_width = desc.style.border_width;
    root_s.radii = desc.style.radii;
    prog.initial_styles.push_back(root_s);

    // Layout descriptor for root container
    LayoutOpDesc root_layout_desc{};
    root_layout_desc.padding = desc.style.padding;
    root_layout_desc.gap = desc.style.gap;
    root_layout_desc.align_items = desc.style.align_items;
    root_layout_desc.justify_content = desc.style.justify_content;
    uint32_t root_desc_idx = static_cast<uint32_t>(prog.layout_descriptors.size());
    prog.layout_descriptors.push_back(root_layout_desc);

    // Paint descriptor for container background
    prog.paint_descriptors.push_back(PaintDesc{
        .kind = PrimitiveKind::RoundedRect,
        .geometry_slot = root_geom,
        .style_slot = root_style,
        .clip_scope = 0,
        .local_order = 0
    });

    // Walk children in tuple
    constexpr size_t num_children = std::tuple_size_v<decltype(desc.children)>;
    uint32_t next_primary_slot = 1;
    uint32_t secondary_geom_slot = 1 + static_cast<uint32_t>(num_children);

    std::vector<SlotRef> child_geom_slots;
    std::apply([&](auto&&... items) {
        auto process_item = [&](auto&& item) {
            using T = std::decay_t<decltype(item)>;
            SlotRef item_geom{next_primary_slot++};
            SlotRef item_style{style_slot++};
            child_geom_slots.push_back(item_geom);

            if constexpr (std::is_same_v<T, TextNode>) {
                uint32_t txt_idx = static_cast<uint32_t>(prog.initial_texts.size());
                prog.initial_texts.push_back(item.text_literal);

                PaintStyleData txt_s{};
                txt_s.fill_color = item.color;
                txt_s.border_color = Color::Transparent();
                prog.initial_styles.push_back(txt_s);

                LayoutOpDesc text_layout{};
                text_layout.width_spec = SizeSpec::Fixed(item.width);
                text_layout.height_spec = SizeSpec::Fixed(item.height);
                uint32_t d_idx = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(text_layout);

                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureText,
                    .descriptor_index = d_idx,
                    .inputs = Range{},
                    .outputs = Range{item_geom.local, 1}
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::GlyphRun,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size()),
                    .text_index = txt_idx
                });
            } else if constexpr (std::is_same_v<T, BadgeNode>) {
                PaintStyleData bg_s{};
                PaintStyleData txt_s{};
                bg_s.radii = CornerRadii{item.height * 0.5f}; // pill shape

                switch (item.variant) {
                    case BadgeVariant::Default:
                        bg_s.fill_color = theme::Zinc800;
                        txt_s.fill_color = theme::Zinc100;
                        break;
                    case BadgeVariant::Secondary:
                        bg_s.fill_color = theme::Zinc700;
                        txt_s.fill_color = theme::Zinc200;
                        break;
                    case BadgeVariant::Outline:
                        bg_s.fill_color = Color::Transparent();
                        bg_s.border_color = theme::Zinc700;
                        bg_s.border_width = 1.0f;
                        txt_s.fill_color = theme::Zinc300;
                        break;
                    case BadgeVariant::Success:
                        bg_s.fill_color = theme::Emerald600;
                        txt_s.fill_color = Color::White();
                        break;
                    case BadgeVariant::Destructive:
                        bg_s.fill_color = theme::Red600;
                        txt_s.fill_color = Color::White();
                        break;
                }

                prog.initial_styles.push_back(bg_s);

                SlotRef txt_style{style_slot++};
                prog.initial_styles.push_back(txt_s);

                uint32_t txt_idx = static_cast<uint32_t>(prog.initial_texts.size());
                prog.initial_texts.push_back(item.label);

                LayoutOpDesc badge_layout{};
                badge_layout.width_spec = SizeSpec::Fixed(item.width);
                badge_layout.height_spec = SizeSpec::Fixed(item.height);
                uint32_t d_idx = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(badge_layout);

                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureFixed,
                    .descriptor_index = d_idx,
                    .inputs = Range{},
                    .outputs = Range{item_geom.local, 1}
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::GlyphRun,
                    .geometry_slot = item_geom,
                    .style_slot = txt_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size()),
                    .text_index = txt_idx
                });
            } else if constexpr (std::is_same_v<T, SeparatorNode>) {
                PaintStyleData sep_s{};
                sep_s.fill_color = item.color;
                prog.initial_styles.push_back(sep_s);

                LayoutOpDesc sep_layout{};
                sep_layout.width_spec = SizeSpec::Fixed(item.width);
                sep_layout.height_spec = SizeSpec::Fixed(item.thickness);
                uint32_t d_idx = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(sep_layout);

                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureFixed,
                    .descriptor_index = d_idx,
                    .inputs = Range{},
                    .outputs = Range{item_geom.local, 1}
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::Rect,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });
            } else if constexpr (std::is_same_v<T, EditableNode>) {
                PaintStyleData field_style{};
                field_style.fill_color = theme::Zinc950;
                field_style.border_color = theme::Zinc700;
                field_style.border_width = 1.0f;
                field_style.radii = CornerRadii{6.0f};
                prog.initial_styles.push_back(field_style);
                SlotRef text_style{style_slot++};
                PaintStyleData foreground{};
                foreground.fill_color = theme::Zinc100;
                prog.initial_styles.push_back(foreground);
                const uint32_t text_index = static_cast<uint32_t>(prog.initial_texts.size());
                prog.initial_texts.push_back(item.initial_value);
                LayoutOpDesc field_layout{};
                field_layout.width_spec = SizeSpec::Fixed(item.width);
                field_layout.height_spec = SizeSpec::Fixed(item.height);
                const uint32_t descriptor = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(field_layout);
                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureFixed,
                    .descriptor_index = descriptor,
                    .outputs = Range{item_geom.local, 1}
                });
                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });
                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::GlyphRun,
                    .geometry_slot = item_geom,
                    .style_slot = text_style,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size()),
                    .text_index = text_index,
                    .text_left_aligned = true,
                    .text_top_aligned = item.height > 40.0f
                });
                prog.input_descriptors.push_back(InputDesc{
                    .geometry_slot = item_geom,
                    .event_handler_id = ~0u,
                    .flags = InputFlags::Focusable | InputFlags::Clickable,
                    .local_order = static_cast<uint32_t>(prog.input_descriptors.size()),
                    .semantic_id = 7,
                    .text_index = text_index,
                    .multiline = item.height > 40.0f
                });
            } else if constexpr (requires { item.initial_percent; item.handler; }) {
                PaintStyleData track_style{};
                track_style.fill_color = theme::Zinc800;
                track_style.radii = CornerRadii{5.0f};
                prog.initial_styles.push_back(track_style);
                SlotRef fill_geom{secondary_geom_slot++};
                SlotRef fill_style{style_slot++};
                PaintStyleData fill_paint{};
                fill_paint.fill_color = theme::Blue500;
                fill_paint.radii = CornerRadii{5.0f};
                prog.initial_styles.push_back(fill_paint);
                LayoutOpDesc slider_layout{};
                slider_layout.width_spec = SizeSpec::Fixed(item.width);
                slider_layout.height_spec = SizeSpec::Fixed(12.0f);
                const uint32_t descriptor = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(slider_layout);
                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureFixed,
                    .descriptor_index = descriptor,
                    .outputs = Range{item_geom.local, 1}
                });
                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });
                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = fill_geom,
                    .style_slot = fill_style,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });
                prog.decorations.push_back(ComponentProgram::Decoration{
                    ComponentProgram::DecorationKind::ProgressFill, item_geom, fill_geom,
                    item.initial_percent
                });
                const uint32_t handler_index = static_cast<uint32_t>(prog.handlers.size());
                prog.handlers.push_back([](void* state, void* event_ctx) {
                    auto* comp_state = static_cast<typename Component::State*>(state);
                    auto* ctx = static_cast<IEventContext*>(event_ctx);
                    EventContext<Component> comp_ctx(ctx->instance, *ctx->tx, ctx->pointer_ratio);
                    (comp_state->*decltype(item.handler)::MethodPtr)(comp_ctx);
                });
                prog.input_descriptors.push_back(InputDesc{
                    .geometry_slot = item_geom,
                    .event_handler_id = handler_index,
                    .flags = InputFlags::Draggable | InputFlags::Focusable | InputFlags::Hoverable,
                    .local_order = static_cast<uint32_t>(prog.input_descriptors.size()),
                    .semantic_id = 8
                });
            } else if constexpr (std::is_same_v<T, ProgressBarNode>) {
                PaintStyleData track_s{};
                track_s.fill_color = theme::Zinc800;
                track_s.radii = CornerRadii{item.height * 0.5f};
                prog.initial_styles.push_back(track_s);

                SlotRef fill_geom{secondary_geom_slot++};
                SlotRef fill_style{style_slot++};

                PaintStyleData fill_s{};
                fill_s.fill_color = theme::Blue500;
                fill_s.radii = CornerRadii{item.height * 0.5f};
                prog.initial_styles.push_back(fill_s);

                LayoutOpDesc prog_layout{};
                prog_layout.width_spec = SizeSpec::Fixed(item.width);
                prog_layout.height_spec = SizeSpec::Fixed(item.height);
                uint32_t d_idx = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(prog_layout);

                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureFixed,
                    .descriptor_index = d_idx,
                    .inputs = Range{},
                    .outputs = Range{item_geom.local, 1}
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = fill_geom,
                    .style_slot = fill_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });
                prog.decorations.push_back(ComponentProgram::Decoration{
                    ComponentProgram::DecorationKind::ProgressFill, item_geom, fill_geom,
                    item.initial_percent
                });
            } else if constexpr (std::is_same_v<T, SurfaceNode> || requires { item.semantic_id; }) {
                PaintStyleData surface_style{};
                surface_style.fill_color = item.fill;
                surface_style.border_color = item.border;
                surface_style.border_width = item.border.a > 0.0f ? 1.0f : 0.0f;
                surface_style.radii = item.radii;
                prog.initial_styles.push_back(surface_style);

                SlotRef caption_style{style_slot++};
                PaintStyleData caption_paint{};
                caption_paint.fill_color = item.foreground;
                prog.initial_styles.push_back(caption_paint);
                SlotRef indicator_geom{};
                SlotRef indicator_style{};
                SlotRef indicator_glyph_style{};
                bool has_indicator = false;
                bool is_radio = false;
                std::string indicator_character;
                if constexpr (requires { item.semantic_id; }) {
                    has_indicator = item.semantic_id == 3 || item.semantic_id == 4;
                    is_radio = item.semantic_id == 4;
                    if (has_indicator) {
                        indicator_character = item.semantic_id == 3 ? "✓" : "";
                        indicator_geom = SlotRef{secondary_geom_slot++};
                        indicator_style = SlotRef{style_slot++};
                        PaintStyleData mark{};
                        mark.fill_color = item.semantic_id == 3 && item.selected ? theme::Blue500 : Color::Transparent();
                        mark.border_color = item.selected ? theme::Blue500 : theme::Zinc500;
                        mark.border_width = 1.5f;
                        mark.radii = CornerRadii{item.semantic_id == 4 ? 9.0f : 4.0f};
                        prog.initial_styles.push_back(mark);
                        indicator_glyph_style = SlotRef{style_slot++};
                        PaintStyleData glyph_style{};
                        glyph_style.fill_color = item.selected ?
                            (item.semantic_id == 3 ? theme::Zinc50 : theme::Blue500) : Color::Transparent();
                        prog.initial_styles.push_back(glyph_style);
                        prog.decorations.push_back(ComponentProgram::Decoration{
                            ComponentProgram::DecorationKind::ControlIndicator,
                            item_geom, indicator_geom, 0.0f
                        });
                    }
                }
                const uint32_t text_index = static_cast<uint32_t>(prog.initial_texts.size());
                prog.initial_texts.push_back(item.label);
                uint32_t indicator_text_index = ~0u;
                if (has_indicator) {
                    indicator_text_index = static_cast<uint32_t>(prog.initial_texts.size());
                    prog.initial_texts.push_back(indicator_character);
                }

                LayoutOpDesc fixed{};
                fixed.width_spec = SizeSpec::Fixed(item.width);
                fixed.height_spec = SizeSpec::Fixed(item.height);
                const uint32_t descriptor = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(fixed);
                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureFixed,
                    .descriptor_index = descriptor,
                    .outputs = Range{item_geom.local, 1}
                });
                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });
                if (has_indicator) {
                    prog.paint_descriptors.push_back(PaintDesc{
                        .kind = PrimitiveKind::RoundedRect,
                        .geometry_slot = indicator_geom,
                        .style_slot = indicator_style,
                        .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                    });
                    prog.paint_descriptors.push_back(PaintDesc{
                        .kind = is_radio ? PrimitiveKind::RoundedRect : PrimitiveKind::GlyphRun,
                        .geometry_slot = indicator_geom,
                        .style_slot = indicator_glyph_style,
                        .local_order = static_cast<uint32_t>(prog.paint_descriptors.size()),
                        .text_index = indicator_text_index,
                        .radio_dot = is_radio
                    });
                }
                if (!item.label.empty()) {
                    prog.paint_descriptors.push_back(PaintDesc{
                        .kind = PrimitiveKind::GlyphRun,
                        .geometry_slot = item_geom,
                        .style_slot = caption_style,
                        .local_order = static_cast<uint32_t>(prog.paint_descriptors.size()),
                        .text_index = text_index,
                        .text_left_aligned = has_indicator,
                        .text_inset = has_indicator ? 38.0f : 12.0f
                    });
                }
                if constexpr (requires { item.semantic_id; }) {
                    const uint32_t handler_index = static_cast<uint32_t>(prog.handlers.size());
                    prog.handlers.push_back([](void* state, void* event_ctx) {
                        auto* comp_state = static_cast<typename Component::State*>(state);
                        auto* ctx = static_cast<IEventContext*>(event_ctx);
                        EventContext<Component> comp_ctx(ctx->instance, *ctx->tx, ctx->pointer_ratio);
                        (comp_state->*decltype(item.handler)::MethodPtr)(comp_ctx);
                    });
                    prog.input_descriptors.push_back(InputDesc{
                        .geometry_slot = item_geom,
                        .event_handler_id = handler_index,
                        .flags = InputFlags::Clickable | InputFlags::Hoverable | InputFlags::Focusable,
                        .local_order = static_cast<uint32_t>(prog.input_descriptors.size()),
                        .semantic_id = item.semantic_id
                    });
                }
            } else if constexpr (requires { item.initial_checked; }) {
                // Switch Track
                PaintStyleData track_s{};
                track_s.fill_color = item.initial_checked ? theme::Blue500 : theme::Zinc800;
                track_s.radii = CornerRadii{12.0f};
                prog.initial_styles.push_back(track_s);

                SlotRef thumb_geom{secondary_geom_slot++};
                SlotRef thumb_style{style_slot++};

                PaintStyleData thumb_s{};
                thumb_s.fill_color = theme::Zinc50;
                thumb_s.radii = CornerRadii{9.0f};
                prog.initial_styles.push_back(thumb_s);

                LayoutOpDesc sw_layout{};
                sw_layout.width_spec = SizeSpec::Fixed(44.0f);
                sw_layout.height_spec = SizeSpec::Fixed(24.0f);
                uint32_t d_idx = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(sw_layout);

                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureFixed,
                    .descriptor_index = d_idx,
                    .inputs = Range{},
                    .outputs = Range{item_geom.local, 1}
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = thumb_geom,
                    .style_slot = thumb_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });
                prog.decorations.push_back(ComponentProgram::Decoration{
                    ComponentProgram::DecorationKind::SwitchThumb, item_geom, thumb_geom,
                    item.initial_checked ? 1.0f : 0.0f
                });

                // Input handler for switch
                uint32_t h_idx = static_cast<uint32_t>(prog.handlers.size());
                if constexpr (decltype(item.handler)::MethodPtr != nullptr) {
                    prog.handlers.push_back([](void* state, void* event_ctx) {
                        auto* comp_state = static_cast<typename Component::State*>(state);
                        auto* ctx = static_cast<IEventContext*>(event_ctx);
                        EventContext<Component> comp_ctx(ctx->instance, *ctx->tx, ctx->pointer_ratio);
                        (comp_state->*decltype(item.handler)::MethodPtr)(comp_ctx);
                    });
                } else {
                    prog.handlers.push_back([](void*, void*) {});
                }

                prog.input_descriptors.push_back(InputDesc{
                    .geometry_slot = item_geom,
                    .event_handler_id = h_idx,
                    .flags = InputFlags::Clickable | InputFlags::Hoverable | InputFlags::Focusable,
                    .local_order = static_cast<uint32_t>(prog.input_descriptors.size()),
                    .semantic_id = 2
                });
                ++input_slot;
            } else if constexpr (requires { item.handler; }) {
                // Button
                PaintStyleData btn_s{};
                PaintStyleData txt_s{};
                btn_s.radii = item.style.radii.top_left > 0.0f ? item.style.radii : CornerRadii{6.0f};

                switch (item.variant) {
                    case ButtonVariant::Default:
                        btn_s.fill_color = item.style.background_color.a > 0.0f ? item.style.background_color : theme::Blue500;
                        btn_s.border_color = item.style.border_color.a > 0.0f ? item.style.border_color : theme::Blue600;
                        btn_s.border_width = 1.0f;
                        txt_s.fill_color = Color::White();
                        break;
                    case ButtonVariant::Secondary:
                        btn_s.fill_color = theme::Zinc800;
                        btn_s.border_color = theme::Zinc700;
                        btn_s.border_width = 1.0f;
                        txt_s.fill_color = theme::Zinc100;
                        break;
                    case ButtonVariant::Outline:
                        btn_s.fill_color = Color::FromRgba8(24, 24, 27, 180);
                        btn_s.border_color = theme::Zinc700;
                        btn_s.border_width = 1.0f;
                        txt_s.fill_color = theme::Zinc100;
                        break;
                    case ButtonVariant::Destructive:
                        btn_s.fill_color = theme::Red500;
                        btn_s.border_color = theme::Red600;
                        btn_s.border_width = 1.0f;
                        txt_s.fill_color = Color::White();
                        break;
                    case ButtonVariant::Ghost:
                        btn_s.fill_color = Color::Transparent();
                        txt_s.fill_color = theme::Zinc100;
                        break;
                    case ButtonVariant::Link:
                        btn_s.fill_color = Color::Transparent();
                        txt_s.fill_color = theme::Blue500;
                        break;
                }

                prog.initial_styles.push_back(btn_s);

                SlotRef btn_text_style{style_slot++};
                prog.initial_styles.push_back(txt_s);

                uint32_t btn_txt_idx = static_cast<uint32_t>(prog.initial_texts.size());
                prog.initial_texts.push_back(item.label);

                LayoutOpDesc btn_layout{};
                btn_layout.width_spec = item.style.width.kind == SizeKind::Fixed ? item.style.width : SizeSpec::Fixed(240.0f);
                btn_layout.height_spec = item.style.height.kind == SizeKind::Fixed ? item.style.height : SizeSpec::Fixed(34.0f);
                uint32_t d_idx = static_cast<uint32_t>(prog.layout_descriptors.size());
                prog.layout_descriptors.push_back(btn_layout);

                prog.layout_ops.push_back(LayoutOp{
                    .opcode = LayoutOpcode::MeasureFixed,
                    .descriptor_index = d_idx,
                    .inputs = Range{},
                    .outputs = Range{item_geom.local, 1}
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::RoundedRect,
                    .geometry_slot = item_geom,
                    .style_slot = item_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size())
                });

                prog.paint_descriptors.push_back(PaintDesc{
                    .kind = PrimitiveKind::GlyphRun,
                    .geometry_slot = item_geom,
                    .style_slot = btn_text_style,
                    .clip_scope = 0,
                    .local_order = static_cast<uint32_t>(prog.paint_descriptors.size()),
                    .text_index = btn_txt_idx
                });

                // Input handler for button
                uint32_t h_idx = static_cast<uint32_t>(prog.handlers.size());
                if constexpr (decltype(item.handler)::MethodPtr != nullptr) {
                    prog.handlers.push_back([](void* state, void* event_ctx) {
                        auto* comp_state = static_cast<typename Component::State*>(state);
                        auto* ctx = static_cast<IEventContext*>(event_ctx);
                        EventContext<Component> comp_ctx(ctx->instance, *ctx->tx, ctx->pointer_ratio);
                        (comp_state->*decltype(item.handler)::MethodPtr)(comp_ctx);
                    });
                } else {
                    prog.handlers.push_back([](void*, void*) {});
                }

                prog.input_descriptors.push_back(InputDesc{
                    .geometry_slot = item_geom,
                    .event_handler_id = h_idx,
                    .flags = InputFlags::Clickable | InputFlags::Hoverable | InputFlags::Focusable,
                    .local_order = static_cast<uint32_t>(prog.input_descriptors.size()),
                    .semantic_id = 1
                });
                ++input_slot;
            }
        };
        (process_item(items), ...);
    }, desc.children);

    // Root Arrange and Measure: all primary child slots are contiguous from 1 to num_children
    Range child_range{1, static_cast<uint32_t>(num_children)};

    prog.layout_ops.push_back(LayoutOp{
        .opcode = desc.type == NodeType::Column ? LayoutOpcode::MeasureColumn :
                  desc.type == NodeType::Overlay ? LayoutOpcode::MeasureOverlay : LayoutOpcode::MeasureRow,
        .descriptor_index = root_desc_idx,
        .inputs = child_range,
        .outputs = Range{root_geom.local, 1}
    });

    prog.layout_ops.push_back(LayoutOp{
        .opcode = desc.type == NodeType::Column ? LayoutOpcode::ArrangeColumn :
                  desc.type == NodeType::Overlay ? LayoutOpcode::ArrangeOverlay : LayoutOpcode::ArrangeRow,
        .descriptor_index = root_desc_idx,
        .inputs = child_range,
        .outputs = Range{root_geom.local, 1}
    });

    prog.total_geometry_slots = secondary_geom_slot;
    prog.total_style_slots = style_slot;
    prog.total_text_slots = static_cast<uint32_t>(prog.initial_texts.size());
    prog.total_data_slots = 4;

    // Connect static Bind hook if present on Component
    if constexpr (requires(UiStorage& s, InstanceId id, typename Component::State& st, const typename Component::Props& p) {
        Component::Bind(s, id, st, p);
    }) {
        prog.update_bindings = [](UiStorage& storage, InstanceId id, void* state, void* props) {
            auto* s = static_cast<typename Component::State*>(state);
            const auto* p = static_cast<const typename Component::Props*>(props);
            Component::Bind(storage, id, *s, *p);
        };
    }

    if constexpr (requires { &Component::Key; }) {
        prog.on_key = [](UiStorage& storage, InstanceId id, void* state, void* props,
            SlotRef slot, uint32_t key, bool ctrl, bool shift) {
            return Component::Key(storage, id, *static_cast<typename Component::State*>(state),
                *static_cast<const typename Component::Props*>(props), slot, key, ctrl, shift);
        };
    }

    return prog;
    }
}

} // namespace arcade_ui
