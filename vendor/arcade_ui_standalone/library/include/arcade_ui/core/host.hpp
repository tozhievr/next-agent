// arcade_ui/core/host.hpp
//
// UiHost: top-level UI controller owning storage, layers, input routing, and frame packet (§3.1, §17).
#pragma once

#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <utility>
#include "types.hpp"
#include "ir.hpp"
#include "storage.hpp"
#include "layers.hpp"
#include "transaction.hpp"
#include "layout.hpp"
#include "text_edit.hpp"

namespace arcade_ui {

// Render command sent to GPU backend (§13.1)
struct DrawCommand {
    PrimitiveKind kind = PrimitiveKind::Rect;
    Rect bounds;
    Color fill_color;
    Color border_color;
    CornerRadii radii;
    float border_width = 0.0f;
    uint32_t texture_id = 0;
    uint64_t visual_order = 0;
    std::string text_str; // For text glyph runs
    bool text_left_aligned = false;
    bool text_top_aligned = false;
    float text_inset = 12.0f;
    float text_scroll_x = 0.0f;
    float text_scroll_y = 0.0f;
    bool text_multiline = false;
};

// Complete packet of commands for one frame (§3.1)
struct FramePacket {
    std::vector<DrawCommand> commands;
    Vec2 viewport_size;
    float dpi = 96.0f;
};

// Pointer event passed to host
struct PointerEvent {
    enum class Kind { Move, Down, Up, Wheel, Leave };
    Kind kind = Kind::Move;
    Vec2 position_dip;
    uint32_t button = 0; // 0 = Left, 1 = Right, 2 = Middle
    float wheel_delta = 0.0f;
};

enum class CompositionPhase { Start, Update, Commit, End };
struct TextInputCaret { InstanceId instance; SlotRef slot; Rect bounds; };

// Type-erased event context passed to handlers (§4.2)
struct IEventContext {
    InstanceId instance;
    TransactionManager* tx = nullptr;
    void* state = nullptr;
    void* props = nullptr;
    float pointer_ratio = 0.0f;
};

class UiHost {
public:
    UiHost();
    ~UiHost();

    // Register a compiled component program
    uint32_t RegisterProgram(ComponentProgram prog);

    // Mount an instance of a component (§4.2)
    template <typename Component>
    InstanceId Mount(uint32_t program_id, typename Component::Props props,
                     LayerBand band = LayerBand::Content) {
        LayerId layer = layers_.PushLayer(band);
        const auto& prog = programs_[program_id];

        InstanceId id = storage_.AllocateInstance(
            program_id, layer,
            prog.total_geometry_slots,
            prog.total_style_slots,
            static_cast<uint32_t>(prog.input_descriptors.size()),
            prog.total_text_slots
        );

        auto* rec = storage_.GetInstance(id);
        rec->props = std::make_shared<typename Component::Props>(std::move(props));
        rec->state = std::make_shared<typename Component::State>();

        // Set initial styles
        for (uint32_t i = 0; i < prog.initial_styles.size() && i < prog.total_style_slots; ++i) {
            storage_.GetStyle(id, SlotRef{i}) = prog.initial_styles[i];
        }

        // Set initial texts
        for (uint32_t i = 0; i < prog.initial_texts.size() && i < prog.total_text_slots; ++i) {
            storage_.GetText(id, i) = prog.initial_texts[i];
        }

        // Default position for root card (offset from top-left)
        if (prog.total_geometry_slots > 0) {
            storage_.GetRect(id, SlotRef{0}).x = 40.0f;
            storage_.GetRect(id, SlotRef{0}).y = 40.0f;
        }

        // Set input descriptors
        for (size_t i = 0; i < prog.input_descriptors.size(); ++i) {
            const auto& in_desc = prog.input_descriptors[i];
            auto& entry = storage_.GetInput(id, static_cast<uint32_t>(i));
            entry.instance = id;
            entry.geom_slot = in_desc.geometry_slot;
            entry.handler_id = in_desc.event_handler_id;
            entry.flags = in_desc.flags;
            entry.local_order = in_desc.local_order;
            entry.text_index = in_desc.text_index;
            entry.multiline = in_desc.multiline;
        }

        // Initial bindings update
        if (prog.update_bindings) {
            prog.update_bindings(storage_, id, rec->state.get(), rec->props.get());
        }

        mounted_instances_.push_back(id);
        dirty_store_.MarkDirty(id, DirtyMask::All);
        return id;
    }

    void Unmount(InstanceId id);

    // Processing input events
    bool DispatchPointer(const PointerEvent& ev);
    bool DispatchKey(uint32_t virtual_key, bool ctrl = false, bool shift = false);
    bool DispatchText(char32_t codepoint);
    bool DispatchComposition(CompositionPhase phase, std::wstring_view text = {}, size_t cursor = 0);
    void CancelTextComposition();
    bool TakeImeCancelRequest() noexcept { return std::exchange(ime_cancel_requested_, false); }
    std::optional<TextInputCaret> GetTextInputCaret();
    bool SetVisible(InstanceId id, bool visible);
    bool Focus(InstanceId id, SlotRef slot);
    [[nodiscard]] bool IsFocused(InstanceId id, SlotRef slot) const noexcept {
        return focused_instance_ == id && focused_slot_ == slot;
    }
    bool IsHovered(InstanceId id, SlotRef slot) const noexcept {
        return hovered_instance_ == id && hovered_slot_ == slot;
    }

    // Layout and commit
    void Update(Vec2 viewport_size, float dpi);

    // Generate draw commands for rendering
    FramePacket BuildFramePacket(Vec2 viewport_size, float dpi);

    LayerStack& Layers() noexcept { return layers_; }
    UiStorage& Storage() noexcept { return storage_; }
    TransactionManager& Transactions() noexcept { return transaction_mgr_; }

private:
    UiStorage storage_;
    LayerStack layers_;
    DirtyStore dirty_store_;
    TransactionManager transaction_mgr_;

    std::vector<ComponentProgram> programs_;
    std::vector<InstanceId> mounted_instances_;

    // Interactive state
    InstanceId hovered_instance_;
    SlotRef hovered_slot_;
    InstanceId pressed_instance_;
    SlotRef pressed_slot_;
    uint32_t pressed_handler_id_ = ~0u;
    InstanceId focused_instance_;
    SlotRef focused_slot_;
    uint32_t focused_text_index_ = ~0u;
    uint32_t focused_handler_id_ = ~0u;
    bool focused_multiline_ = false;
    struct FocusTarget { InstanceId instance; SlotRef slot; };
    std::optional<FocusTarget> modal_restore_;
    LayerId active_modal_;
    InstanceId composition_owner_;
    SlotRef composition_slot_;
    bool composition_cancelled_ = false;
    bool ime_cancel_requested_ = false;
    void ValidateFocus();
    struct EditorEntry { InstanceId instance; uint32_t text_index; TextEditModel model; float scroll_x = 0; float scroll_y = 0; };
    std::vector<EditorEntry> editors_;
    TextEditModel* FocusedEditor();
    EditorEntry* FocusedEditorEntry();
    void SyncFocusedEditor();
    void KeepCaretVisible();
    uint32_t CaretFromPoint(Vec2 point);
    std::chrono::steady_clock::time_point focus_changed_at_ = std::chrono::steady_clock::now();
    void Activate(InstanceId id, uint32_t handler_id, float pointer_ratio);
};

} // namespace arcade_ui
