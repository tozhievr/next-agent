// arcade_ui/core/ir.hpp
//
// Minimal flat compile-time & runtime Intermediate Representation (§5.2).
// Fixed-size structures, contiguous ranges, no object trees or VDOM.
#pragma once

#include <span>
#include <vector>
#include <functional>
#include "types.hpp"

namespace arcade_ui {

// Local slot reference inside an instance
struct SlotRef {
    uint32_t local = ~0u;
    constexpr bool IsValid() const noexcept { return local != ~0u; }
    constexpr auto operator<=>(const SlotRef&) const noexcept = default;
};

// Contiguous range within flat storage or arrays
struct Range {
    uint32_t first = 0;
    uint32_t count = 0;

    constexpr uint32_t End() const noexcept { return first + count; }
    constexpr bool Empty() const noexcept { return count == 0; }
    constexpr auto operator<=>(const Range&) const noexcept = default;
};

// --- Bindings (§5.2) -------------------------------------------------------
struct SourceRef {
    enum class Kind : uint8_t { Prop = 0, State = 1, Slot = 2 };
    Kind kind = Kind::Prop;
    uint32_t offset = 0; // byte offset or member ID
};

struct BindingDesc {
    Range dependencies;     // range into SourceRef table
    SlotRef destination;   // slot where computed value is placed
    uint32_t evaluator_id;  // index of typed evaluator function
    DirtyMask affects = DirtyMask::None;
};

// --- Layout IR (§5.2, §8.3) ------------------------------------------------
enum class LayoutOpcode : uint8_t {
    MeasureFixed = 0,
    MeasureText = 1,
    MeasureRow = 2,
    MeasureColumn = 3,
    MeasureOverlay = 4,
    ArrangeRow = 5,
    ArrangeColumn = 6,
    ArrangeOverlay = 7,
    ResolvePaintBounds = 8
};

struct LayoutOpDesc {
    Padding padding;
    float gap = 0.0f;
    Alignment align_items = Alignment::Start;
    Alignment justify_content = Alignment::Start;
    SizeSpec width_spec = SizeSpec::Intrinsic();
    SizeSpec height_spec = SizeSpec::Intrinsic();
};

struct LayoutOp {
    LayoutOpcode opcode = LayoutOpcode::MeasureFixed;
    uint32_t descriptor_index = 0; // index into LayoutOpDesc array
    Range inputs;                 // child slots or sub-ranges to measure/arrange
    Range outputs;                // slot or range of slots to store results
};

// --- Paint IR (§5.2, §13.1) ------------------------------------------------
enum class PrimitiveKind : uint8_t {
    Rect = 0,
    RoundedRect = 1,
    Border = 2,
    ImageQuad = 3,
    GlyphRun = 4
};

struct PaintStyleData {
    Color fill_color = Color::White();
    Color border_color = Color::Transparent();
    CornerRadii radii;
    float border_width = 0.0f;
    uint32_t texture_id = 0; // 0 = flat quad, >0 = font atlas or texture
};

struct PaintDesc {
    PrimitiveKind kind = PrimitiveKind::Rect;
    SlotRef geometry_slot;  // SlotRef storing Rect
    SlotRef style_slot;     // SlotRef storing PaintStyleData
    uint32_t clip_scope = 0;
    uint32_t local_order = 0;
    uint32_t text_index = ~0u; // Index into initial_texts table if GlyphRun
    bool text_left_aligned = false;
    bool text_top_aligned = false;
    float text_inset = 12.0f;
    bool radio_dot = false;
};

// --- Input IR (§6.1, §11.4) ------------------------------------------------
enum class InputFlags : uint16_t {
    None        = 0,
    Clickable   = 1 << 0,
    Hoverable   = 1 << 1,
    Focusable   = 1 << 2,
    Draggable   = 1 << 3,
    Scrollable  = 1 << 4
};

constexpr InputFlags operator|(InputFlags a, InputFlags b) noexcept {
    return static_cast<InputFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}
constexpr bool HasFlag(InputFlags flags, InputFlags flag) noexcept {
    return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

struct InputDesc {
    SlotRef geometry_slot;
    uint32_t event_handler_id = 0;
    InputFlags flags = InputFlags::None;
    uint32_t local_order = 0;
    uint32_t semantic_id = 0;
    uint32_t text_index = ~0u; // editable field, when present
    bool multiline = false;
};

// --- Region Descriptor (§5.2) ----------------------------------------------
struct RegionDesc {
    Range layout_ops;
    Range paint_descriptors;
    Range input_descriptors;
    Range stateful_instances;
};

class UiStorage;

// Static Component Program (§3.1, §5.2) ---------------------------------
// Compiled flat program. Lifetime: whole process, completely immutable.
struct ComponentProgram {
    uint32_t program_id = 0;
    std::string_view name;

    // Slots requirement
    uint32_t total_geometry_slots = 0;
    uint32_t total_style_slots = 0;
    uint32_t total_data_slots = 0;
    uint32_t total_text_slots = 0;

    // Read-only tables
    std::vector<SourceRef> sources;
    std::vector<BindingDesc> bindings;
    std::vector<LayoutOpDesc> layout_descriptors;
    std::vector<LayoutOp> layout_ops;
    std::vector<PaintDesc> paint_descriptors;
    std::vector<InputDesc> input_descriptors;
    std::vector<RegionDesc> regions;

    // Initial styles and texts for static slots
    std::vector<PaintStyleData> initial_styles;
    std::vector<std::string> initial_texts;

    // Secondary geometry placed after layout. Bind hooks may override it.
    enum class DecorationKind : uint8_t { ProgressFill, SwitchThumb, ControlIndicator };
    struct Decoration {
        DecorationKind kind;
        SlotRef parent;
        SlotRef child;
        float value = 0.0f;
    };
    std::vector<Decoration> decorations;

    // Evaluator thunks
    using EvaluatorFn = std::function<void(void* instance, void* dest)>;
    std::vector<EvaluatorFn> evaluators;

    // Input handlers
    using HandlerFn = std::function<void(void* state, void* event_ctx)>;
    std::vector<HandlerFn> handlers;

    // Dynamic reactive binding update hook: maps Component State/Props to UiStorage
    using UpdateBindingsFn = std::function<void(UiStorage& storage, InstanceId id, void* state, void* props)>;
    UpdateBindingsFn update_bindings;
    // Optional component keyboard behavior before generic host shortcuts.
    using KeyHandlerFn = std::function<bool(UiStorage&, InstanceId, void*, void*, SlotRef,
                                            uint32_t, bool, bool)>;
    KeyHandlerFn on_key;
    std::function<void(void*, void*)> on_blur;
};

} // namespace arcade_ui
