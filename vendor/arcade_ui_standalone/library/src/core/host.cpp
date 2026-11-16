// arcade_ui/src/core/host.cpp
#include <arcade_ui/core/host.hpp>
#include <arcade_ui/core/theme.hpp>
#include <arcade_ui/win32/text_engine.hpp>
#include <algorithm>
#include <chrono>
#include <windows.h>

namespace {
std::string EncodeUtf8(char32_t cp) {
    if ((cp < 0x20 && cp != U'\n' && cp != U'\t') || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {};
    std::string out;
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

bool CopyClipboard(std::wstring_view value) {
    if (!OpenClipboard(nullptr)) return false;
    if (!EmptyClipboard()) { CloseClipboard(); return false; }
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (!block) { CloseClipboard(); return false; }
    auto* destination = static_cast<wchar_t*>(GlobalLock(block));
    if (!destination) { GlobalFree(block); CloseClipboard(); return false; }
    std::copy(value.begin(), value.end(), destination);
    destination[value.size()] = L'\0';
    GlobalUnlock(block);
    if (!SetClipboardData(CF_UNICODETEXT, block)) { GlobalFree(block); CloseClipboard(); return false; }
    CloseClipboard();
    return true;
}

std::wstring PasteClipboard() {
    std::wstring value;
    if (!OpenClipboard(nullptr)) return value;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* source = static_cast<const wchar_t*>(GlobalLock(data))) {
            value = source;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return value;
}
}

namespace arcade_ui {

UiHost::UiHost()
    : transaction_mgr_(storage_, dirty_store_) {}

UiHost::~UiHost() = default;

uint32_t UiHost::RegisterProgram(ComponentProgram prog) {
    const uint32_t id = static_cast<uint32_t>(programs_.size());
    prog.program_id = id;
    programs_.push_back(std::move(prog));
    return id;
}

void UiHost::Unmount(InstanceId id) {
    if (!storage_.IsAlive(id)) return;
    if (focused_instance_ == id) {
        CancelTextComposition();
        focused_instance_ = {};
        focused_slot_ = {};
        focused_text_index_ = ~0u;
    }
    auto* rec = storage_.GetInstance(id);
    if (rec) {
        layers_.RemoveLayer(rec->layer);
    }
    storage_.FreeInstance(id);
    std::erase_if(editors_, [id](const EditorEntry& entry) { return entry.instance == id; });
    std::erase(mounted_instances_, id);
}

bool UiHost::SetVisible(InstanceId id, bool visible) {
    const auto* rec = storage_.GetInstance(id);
    if (rec && !visible && focused_instance_ == id) {
        CancelTextComposition();
        const auto& program = programs_[rec->program_id];
        if (program.on_blur) program.on_blur(rec->state.get(), rec->props.get());
        focused_instance_ = {};
        focused_slot_ = {};
        focused_text_index_ = ~0u;
    }
    return rec && layers_.SetVisible(rec->layer, visible);
}

void UiHost::Activate(InstanceId id, uint32_t handler_id, float pointer_ratio) {
    const auto* rec = storage_.GetInstance(id);
    if (!rec) return;
    const auto& prog = programs_[rec->program_id];
    if (handler_id >= prog.handlers.size()) return;
    IEventContext ctx{.instance = id, .tx = &transaction_mgr_,
                      .state = rec->state.get(), .props = rec->props.get(),
                      .pointer_ratio = std::clamp(pointer_ratio, 0.0f, 1.0f)};
    prog.handlers[handler_id](rec->state.get(), &ctx);
    dirty_store_.MarkDirty(id, DirtyMask::All);
}

bool UiHost::Focus(InstanceId id, SlotRef slot) {
    const auto* rec = storage_.GetInstance(id);
    const auto* layer = rec ? layers_.FindLayer(rec->layer) : nullptr;
    if (!layer || !layer->visible || layer->input_policy == InputPolicy::PassThrough) return false;
    for (const auto* candidate : layers_.HitTestOrder()) {
        if (candidate->input_policy == InputPolicy::Modal && candidate->SortKey() > layer->SortKey())
            return false;
    }
    for (uint32_t i = 0; i < rec->input_count; ++i) {
        const auto& input = storage_.GetInput(id, i);
        const auto& rect = storage_.GetRect(id, input.geom_slot);
        if (input.geom_slot != slot || !input.enabled || rect.width <= 0 || rect.height <= 0 ||
            !HasFlag(input.flags, InputFlags::Focusable)) continue;
        const bool changed = focused_instance_ != id || focused_slot_ != slot;
        if (changed) CancelTextComposition();
        if (focused_instance_ != id) {
            if (const auto* old = storage_.GetInstance(focused_instance_)) {
                const auto& program = programs_[old->program_id];
                if (program.on_blur) program.on_blur(old->state.get(), old->props.get());
            }
        }
        focused_instance_ = id;
        focused_slot_ = slot;
        focused_text_index_ = input.text_index;
        focused_handler_id_ = input.handler_id;
        focused_multiline_ = input.multiline;
        if (changed) focus_changed_at_ = std::chrono::steady_clock::now();
        KeepCaretVisible();
        return true;
    }
    return false;
}

void UiHost::ValidateFocus() {
    const LayerDesc* modal = nullptr;
    for (const auto* layer : layers_.HitTestOrder()) {
        if (layer->input_policy == InputPolicy::Modal) { modal = layer; break; }
    }
    const LayerId next_modal = modal ? modal->id : LayerId{};
    if (next_modal != active_modal_) {
        pressed_instance_ = {}; pressed_slot_ = {}; pressed_handler_id_ = ~0u;
        if (modal && !active_modal_.IsValid()) modal_restore_ = FocusTarget{focused_instance_, focused_slot_};
        active_modal_ = next_modal;
        if (!modal && modal_restore_) {
            auto target = *modal_restore_;
            modal_restore_.reset();
            Focus(target.instance, target.slot);
        }
    }
    if (Focus(focused_instance_, focused_slot_)) return;
    const bool had_focus = focused_instance_.IsValid();
    const auto old_instance = focused_instance_;
    CancelTextComposition();
    focused_instance_ = {};
    focused_slot_ = {};
    focused_text_index_ = ~0u;
    if (!modal && !had_focus) return;
    if (const auto* previous = storage_.GetInstance(old_instance)) {
        for (uint32_t i = 0; i < previous->input_count; ++i)
            if (Focus(old_instance, storage_.GetInput(old_instance, i).geom_slot)) return;
    }
    for (const auto* layer : layers_.PaintOrder()) {
        if (modal && layer->SortKey() < modal->SortKey()) continue;
        for (auto id : mounted_instances_) {
            const auto* rec = storage_.GetInstance(id);
            if (!rec || rec->layer != layer->id) continue;
            for (uint32_t i = 0; i < rec->input_count; ++i) {
                if (Focus(id, storage_.GetInput(id, i).geom_slot)) return;
            }
        }
    }
}

bool UiHost::DispatchText(char32_t codepoint) {
    auto* editor = FocusedEditor();
    if (!editor) return false;
    if (codepoint == U'\r') codepoint = U'\n';
    if (auto encoded = EncodeUtf8(codepoint); !encoded.empty()) {
        if (editor->Insert(Utf8ToUtf16(encoded), focused_multiline_)) SyncFocusedEditor();
    }
    return true;
}

bool UiHost::DispatchComposition(CompositionPhase phase, std::wstring_view text, size_t cursor) {
    if (phase == CompositionPhase::End) {
        for (auto& editor : editors_) editor.model.CancelComposition();
        composition_owner_ = {}; composition_slot_ = {}; composition_cancelled_ = false;
        return true;
    }
    auto* editor = FocusedEditor();
    if (!editor) return false;
    if (phase == CompositionPhase::Start || !composition_owner_.IsValid()) {
        composition_owner_ = focused_instance_;
        composition_slot_ = focused_slot_;
        composition_cancelled_ = false;
        editor->BeginComposition();
    }
    if (composition_cancelled_ || composition_owner_ != focused_instance_ || composition_slot_ != focused_slot_)
        return false;
    if (phase == CompositionPhase::Update) editor->UpdateComposition(text, cursor);
    if (phase == CompositionPhase::Commit) {
        if (editor->CommitComposition(text, focused_multiline_)) SyncFocusedEditor();
    }
    focus_changed_at_ = std::chrono::steady_clock::now();
    KeepCaretVisible();
    return true;
}

void UiHost::CancelTextComposition() {
    bool active = false;
    for (auto& editor : editors_) {
        active |= editor.model.IsComposing();
        editor.model.CancelComposition();
    }
    if (active) { composition_cancelled_ = true; ime_cancel_requested_ = true; }
}

std::optional<TextInputCaret> UiHost::GetTextInputCaret() {
    auto* entry = FocusedEditorEntry();
    if (!entry) return std::nullopt;
    const auto& rect = storage_.GetRect(focused_instance_, focused_slot_);
    const auto metrics = DirectWriteTextEngine::Instance().CaretAt(entry->model.DisplayText(),
        static_cast<uint32_t>(entry->model.DisplayCaret()), rect.width - 24, focused_multiline_);
    return TextInputCaret{focused_instance_, focused_slot_, Rect{
        rect.x + 12 + metrics.x - entry->scroll_x,
        rect.y + (focused_multiline_ ? 8 : (rect.height - 18) * 0.5f) + metrics.y - entry->scroll_y,
        1.5f, metrics.height}};
}

UiHost::EditorEntry* UiHost::FocusedEditorEntry() {
    if (!storage_.IsAlive(focused_instance_) || focused_text_index_ == ~0u) return nullptr;
    auto it = std::find_if(editors_.begin(), editors_.end(), [this](const EditorEntry& entry) {
        return entry.instance == focused_instance_ && entry.text_index == focused_text_index_;
    });
    if (it != editors_.end()) return &*it;
    auto& value = storage_.GetText(focused_instance_, focused_text_index_);
    editors_.push_back({focused_instance_, focused_text_index_, TextEditModel(value)});
    return &editors_.back();
}

TextEditModel* UiHost::FocusedEditor() {
    auto* entry = FocusedEditorEntry();
    return entry ? &entry->model : nullptr;
}

void UiHost::SyncFocusedEditor() {
    auto* editor = FocusedEditor();
    if (!editor) return;
    storage_.GetText(focused_instance_, focused_text_index_) = editor->Utf8();
    focus_changed_at_ = std::chrono::steady_clock::now();
    KeepCaretVisible();
    dirty_store_.MarkDirty(focused_instance_, DirtyMask::Paint);
}

void UiHost::KeepCaretVisible() {
    auto* entry = FocusedEditorEntry();
    if (!entry) return;
    const Rect& rect = storage_.GetRect(focused_instance_, focused_slot_);
    auto caret = DirectWriteTextEngine::Instance().CaretAt(entry->model.DisplayText(),
        static_cast<uint32_t>(entry->model.DisplayCaret()), rect.width - 24.0f, focused_multiline_);
    const float available_x = (std::max)(1.0f, rect.width - 24.0f);
    const float available_y = (std::max)(1.0f, rect.height - 16.0f);
    if (caret.x < entry->scroll_x) entry->scroll_x = caret.x;
    if (caret.x > entry->scroll_x + available_x - 3.0f)
        entry->scroll_x = caret.x - available_x + 3.0f;
    if (caret.y < entry->scroll_y) entry->scroll_y = caret.y;
    if (caret.y + caret.height > entry->scroll_y + available_y)
        entry->scroll_y = caret.y + caret.height - available_y;
}

uint32_t UiHost::CaretFromPoint(Vec2 point) {
    auto* entry = FocusedEditorEntry();
    if (!entry) return 0;
    const Rect& rect = storage_.GetRect(focused_instance_, focused_slot_);
    const float x = point.x - rect.x - 12.0f + entry->scroll_x;
    const float y = point.y - rect.y - (focused_multiline_ ? 8.0f : (rect.height - 18.0f) * 0.5f) + entry->scroll_y;
    return DirectWriteTextEngine::Instance().HitTestPoint(entry->model.Text(), x, y,
        rect.width - 24.0f, focused_multiline_);
}

bool UiHost::DispatchKey(uint32_t virtual_key, bool ctrl, bool shift) {
    ValidateFocus();
    if (auto* editor = FocusedEditor(); editor && editor->IsComposing()) return true;
    if (virtual_key == 9) { // Tab
        const auto layers = layers_.PaintOrder();
        uint64_t barrier = 0;
        for (const auto* layer : layers)
            if (layer->input_policy == InputPolicy::Modal) barrier = layer->SortKey();
        std::vector<std::pair<InstanceId, UiStorage::InputEntry>> focusable;
        for (const auto* layer : layers) {
            if (layer->SortKey() < barrier || layer->input_policy == InputPolicy::PassThrough) continue;
            for (auto id : mounted_instances_) {
                const auto* rec = storage_.GetInstance(id);
                if (!rec || rec->layer != layer->id) continue;
                for (uint32_t i = 0; i < rec->input_count; ++i) {
                    const auto& entry = storage_.GetInput(id, i);
                    const auto& rect = storage_.GetRect(id, entry.geom_slot);
                    if (entry.enabled && rect.width > 0 && rect.height > 0 &&
                        HasFlag(entry.flags, InputFlags::Focusable)) focusable.emplace_back(id, entry);
                }
            }
        }
        if (focusable.empty()) return false;
        auto it = std::find_if(focusable.begin(), focusable.end(), [&](const auto& item) {
            return item.first == focused_instance_ && item.second.geom_slot == focused_slot_;
        });
        size_t index = it == focusable.end() ? (shift ? focusable.size() - 1 : 0) :
            (static_cast<size_t>(it - focusable.begin()) + (shift ? focusable.size() - 1 : 1)) % focusable.size();
        return Focus(focusable[index].first, focusable[index].second.geom_slot);
    }
    if (!storage_.IsAlive(focused_instance_)) return false;
    const auto* focused_record = storage_.GetInstance(focused_instance_);
    const auto& focused_program = programs_[focused_record->program_id];
    if (focused_program.on_key && focused_program.on_key(storage_, focused_instance_,
        focused_record->state.get(), focused_record->props.get(), focused_slot_, virtual_key, ctrl, shift)) {
        dirty_store_.MarkDirty(focused_instance_, DirtyMask::All);
        return true;
    }
    if (auto* editor = FocusedEditor()) {
        bool changed = false;
        if (ctrl) {
            switch (virtual_key) {
                case 'A': editor->SelectAll(); break;
                case 'C': if (editor->HasSelection()) CopyClipboard(editor->SelectedText()); break;
                case 'X': if (editor->HasSelection() && CopyClipboard(editor->SelectedText())) changed = editor->Backspace(); break;
                case 'V': changed = editor->Insert(PasteClipboard(), focused_multiline_); break;
                case 'Z': changed = shift ? editor->Redo() : editor->Undo(); break;
                case 'Y': changed = editor->Redo(); break;
                default: break;
            }
            if (virtual_key == 'A' || virtual_key == 'C' || virtual_key == 'X' ||
                virtual_key == 'V' || virtual_key == 'Z' || virtual_key == 'Y') {
                if (changed) SyncFocusedEditor();
                else { KeepCaretVisible(); focus_changed_at_ = std::chrono::steady_clock::now(); }
                return true;
            }
        }
        switch (virtual_key) {
            case VK_BACK: changed = editor->Backspace(ctrl); break;
            case VK_DELETE: changed = editor->DeleteForward(ctrl); break;
            case VK_LEFT: editor->MoveHorizontal(false, shift, ctrl); break;
            case VK_RIGHT: editor->MoveHorizontal(true, shift, ctrl); break;
            case VK_HOME: editor->MoveLineBoundary(false, shift, ctrl); break;
            case VK_END: editor->MoveLineBoundary(true, shift, ctrl); break;
            case VK_UP:
            case VK_DOWN: {
                if (!focused_multiline_) break;
                const Rect& rect = storage_.GetRect(focused_instance_, focused_slot_);
                auto caret = DirectWriteTextEngine::Instance().CaretAt(editor->Text(),
                    static_cast<uint32_t>(editor->Caret()), rect.width - 24.0f, true);
                auto next = DirectWriteTextEngine::Instance().HitTestPoint(editor->Text(),
                    caret.x, caret.y + (virtual_key == VK_UP ? -caret.height * 0.5f : caret.height * 1.5f),
                    rect.width - 24.0f, true);
                editor->SetCaret(next, shift);
                break;
            }
            case VK_RETURN: if (focused_multiline_) break; return false;
            default: return false;
        }
        if (changed) SyncFocusedEditor();
        else { KeepCaretVisible(); focus_changed_at_ = std::chrono::steady_clock::now(); }
        return true;
    }
    if ((virtual_key == 13 || virtual_key == 32) && focused_text_index_ == ~0u) {
        Activate(focused_instance_, focused_handler_id_, 0.5f);
        return true;
    }
    return false;
}

bool UiHost::DispatchPointer(const PointerEvent& ev) {
    ValidateFocus();
    if (ev.kind == PointerEvent::Kind::Down) {
        if (const auto* focused = storage_.GetInstance(focused_instance_)) {
            if (!storage_.GetRect(focused_instance_, SlotRef{0}).Contains(ev.position_dip)) {
                const auto& program = programs_[focused->program_id];
                if (program.on_blur) program.on_blur(focused->state.get(), focused->props.get());
            }
        }
    }
    if (ev.kind == PointerEvent::Kind::Leave) {
        hovered_instance_ = {};
        hovered_slot_ = {};
        pressed_instance_ = {};
        pressed_slot_ = {};
        pressed_handler_id_ = ~0u;
        return false;
    }

    // Top-to-bottom layer traversal (§11.4)
    auto hit_layers = layers_.HitTestOrder();

    if (ev.kind == PointerEvent::Kind::Move) {
        if (pressed_instance_ == focused_instance_ && pressed_slot_ == focused_slot_ &&
            focused_text_index_ != ~0u) {
            if (auto* editor = FocusedEditor()) {
                editor->SetCaret(CaretFromPoint(ev.position_dip), true);
                KeepCaretVisible();
                focus_changed_at_ = std::chrono::steady_clock::now();
                return true;
            }
        }
        if (storage_.IsAlive(pressed_instance_) && pressed_handler_id_ != ~0u) {
            const auto* rec = storage_.GetInstance(pressed_instance_);
            for (uint32_t i = 0; i < rec->input_count; ++i) {
                const auto& entry = storage_.GetInput(pressed_instance_, i);
                if (entry.geom_slot == pressed_slot_ && HasFlag(entry.flags, InputFlags::Draggable)) {
                    const auto& geom = storage_.GetRect(pressed_instance_, pressed_slot_);
                    Activate(pressed_instance_, pressed_handler_id_,
                             geom.width > 0.0f ? (ev.position_dip.x - geom.x) / geom.width : 0.0f);
                    return true;
                }
            }
        }
        bool hit_interactive = false;
        for (const auto* layer : hit_layers) {
            if (!layer || !layer->visible) continue;
            if (layer->input_policy == InputPolicy::PassThrough) continue;

            for (auto inst_id : mounted_instances_) {
                const auto* rec = storage_.GetInstance(inst_id);
                if (!rec || rec->layer != layer->id) continue;

                const auto& prog = programs_[rec->program_id];
                for (size_t i = 0; i < prog.input_descriptors.size(); ++i) {
                    const auto& in_entry = storage_.GetInput(inst_id, static_cast<uint32_t>(i));
                    const Rect& geom = storage_.GetRect(inst_id, in_entry.geom_slot);
                    if (!in_entry.enabled || geom.width <= 0 || geom.height <= 0) continue;

                    if (geom.Contains(ev.position_dip)) {
                        hovered_instance_ = inst_id;
                        hovered_slot_ = in_entry.geom_slot;
                        hit_interactive = true;
                        break;
                    }
                }
                if (hit_interactive) break;
            }
            if (hit_interactive) break;
            if (layer->input_policy == InputPolicy::Modal || layer->input_policy == InputPolicy::Blocking) break;
        }

        if (!hit_interactive) {
            hovered_instance_ = {};
            hovered_slot_ = {};
        }
        return hit_interactive;
    }

    for (const auto* layer : hit_layers) {
        if (!layer || !layer->visible) continue;
        if (layer->input_policy == InputPolicy::PassThrough) continue;

        // Check instances belonging to this layer
        for (auto inst_id : mounted_instances_) {
            const auto* rec = storage_.GetInstance(inst_id);
            if (!rec || rec->layer != layer->id) continue;

            const auto& prog = programs_[rec->program_id];
            for (size_t i = 0; i < prog.input_descriptors.size(); ++i) {
                const auto& in_entry = storage_.GetInput(inst_id, static_cast<uint32_t>(i));
                const Rect& geom = storage_.GetRect(inst_id, in_entry.geom_slot);
                if (!in_entry.enabled || geom.width <= 0 || geom.height <= 0) continue;

                if (geom.Contains(ev.position_dip)) {
                    if (ev.kind == PointerEvent::Kind::Down && ev.button == 0) {
                        Focus(inst_id, in_entry.geom_slot);
                        focus_changed_at_ = std::chrono::steady_clock::now();
                        pressed_instance_ = inst_id;
                        pressed_slot_ = in_entry.geom_slot;
                        pressed_handler_id_ = in_entry.handler_id;
                        if (focused_text_index_ != ~0u) {
                            CancelTextComposition();
                            if (auto* editor = FocusedEditor())
                                editor->SetCaret(CaretFromPoint(ev.position_dip));
                            KeepCaretVisible();
                        }
                        if (HasFlag(in_entry.flags, InputFlags::Draggable)) {
                            Activate(inst_id, in_entry.handler_id,
                                     geom.width > 0.0f ? (ev.position_dip.x - geom.x) / geom.width : 0.0f);
                        }
                        return true;
                    } else if (ev.kind == PointerEvent::Kind::Up && ev.button == 0) {
                        if (pressed_instance_ == inst_id && pressed_slot_ == in_entry.geom_slot &&
                            pressed_handler_id_ < prog.handlers.size() &&
                            !HasFlag(in_entry.flags, InputFlags::Draggable)) {
                            Activate(inst_id, pressed_handler_id_,
                                     geom.width > 0.0f ? (ev.position_dip.x - geom.x) / geom.width : 0.0f);
                        }
                        pressed_instance_ = {};
                        pressed_slot_ = {};
                        pressed_handler_id_ = ~0u;
                        return true;
                    }
                    return (layer->input_policy == InputPolicy::Interactive ||
                            layer->input_policy == InputPolicy::Blocking ||
                            layer->input_policy == InputPolicy::Modal);
                }
            }
        }

        if (layer->input_policy == InputPolicy::Blocking || layer->input_policy == InputPolicy::Modal) {
            // Block further propagation to underlying layers
            return true;
        }
    }
    if (ev.kind == PointerEvent::Kind::Up) {
        pressed_instance_ = {};
        pressed_slot_ = {};
        pressed_handler_id_ = ~0u;
    }
    if (ev.kind == PointerEvent::Kind::Down) {
        CancelTextComposition();
        focused_instance_ = {}; focused_slot_ = {}; focused_text_index_ = ~0u;
    }
    return false;
}

void UiHost::Update(Vec2 viewport_size, float dpi) {
    (void)dpi;
    // 1. Commit pending transactions
    transaction_mgr_.Commit(programs_);

    // 2. Execute reactive bindings updates
    for (auto inst_id : mounted_instances_) {
        auto* rec = storage_.GetInstance(inst_id);
        if (!rec) continue;

        const auto& prog = programs_[rec->program_id];
        if (prog.update_bindings) {
            prog.update_bindings(storage_, inst_id, rec->state.get(), rec->props.get());
        }
    }

    // 3. Execute layout for each mounted instance
    BoxConstraints constraints(0.0f, viewport_size.x, 0.0f, viewport_size.y);

    for (auto inst_id : mounted_instances_) {
        const auto* rec = storage_.GetInstance(inst_id);
        if (!rec) continue;

        const auto& prog = programs_[rec->program_id];
        LayoutEngine::ExecuteLayout(storage_, inst_id,
                                   prog.layout_descriptors,
                                   prog.layout_ops,
                                   constraints);
    }

    // 4. Place built-in secondary geometry after its parent has been arranged.
    for (auto inst_id : mounted_instances_) {
        const auto* rec = storage_.GetInstance(inst_id);
        if (!rec) continue;
        const auto& prog = programs_[rec->program_id];
        for (const auto& decoration : prog.decorations) {
            const Rect& parent = storage_.GetRect(inst_id, decoration.parent);
            Rect& child = storage_.GetRect(inst_id, decoration.child);
            if (decoration.kind == ComponentProgram::DecorationKind::ProgressFill) {
                child = Rect{parent.x, parent.y,
                    parent.width * std::clamp(decoration.value, 0.0f, 100.0f) / 100.0f,
                    parent.height};
            } else if (decoration.kind == ComponentProgram::DecorationKind::SwitchThumb) {
                child = Rect{parent.x + (decoration.value > 0.0f ? 22.0f : 4.0f),
                    parent.y + 3.0f, 18.0f, 18.0f};
            } else {
                child = Rect{parent.x + 9.0f, parent.y + (parent.height - 18.0f) * 0.5f,
                    18.0f, 18.0f};
            }
        }
    }

    // 5. Post-layout bindings may override the initial decoration values.
    for (auto inst_id : mounted_instances_) {
        auto* rec = storage_.GetInstance(inst_id);
        if (!rec) continue;

        const auto& prog = programs_[rec->program_id];
        if (prog.update_bindings) {
            prog.update_bindings(storage_, inst_id, rec->state.get(), rec->props.get());
        }
    }
    for (auto& editor : editors_) {
        if (!storage_.IsAlive(editor.instance)) continue;
        const auto& text = storage_.GetText(editor.instance, editor.text_index);
        if (text != editor.model.Utf8()) {
            if (editor.model.IsComposing()) CancelTextComposition();
            editor.model = TextEditModel(text);
            editor.scroll_x = editor.scroll_y = 0.0f;
        }
    }
    for (auto id : mounted_instances_) {
        auto* record = storage_.GetInstance(id);
        if (record && record->requested_focus.IsValid()) {
            const auto target = record->requested_focus;
            record->requested_focus = {};
            Focus(id, target);
        }
    }
    ValidateFocus();
}

FramePacket UiHost::BuildFramePacket(Vec2 viewport_size, float dpi) {
    FramePacket packet;
    packet.viewport_size = viewport_size;
    packet.dpi = dpi;

    // Iterate through layers in paint order (bottom-to-top) (§7.2)
    auto layers = layers_.PaintOrder();

    for (const auto* layer : layers) {
        if (!layer || !layer->visible) continue;

        for (auto inst_id : mounted_instances_) {
            const auto* rec = storage_.GetInstance(inst_id);
            if (!rec || rec->layer != layer->id) continue;

            const auto& prog = programs_[rec->program_id];
            for (const auto& paint_desc : prog.paint_descriptors) {
                const Rect& geom = storage_.GetRect(inst_id, paint_desc.geometry_slot);
                if (geom.width <= 0 || geom.height <= 0) continue;
                const PaintStyleData& style = storage_.GetStyle(inst_id, paint_desc.style_slot);

                DrawCommand cmd{};
                cmd.kind = paint_desc.kind;
                cmd.bounds = geom;
                cmd.fill_color = style.fill_color;
                cmd.border_color = style.border_color;
                cmd.radii = style.radii;
                cmd.border_width = style.border_width;
                cmd.texture_id = style.texture_id;
                cmd.visual_order = layer->SortKey() | paint_desc.local_order;
                cmd.text_left_aligned = paint_desc.text_left_aligned;
                cmd.text_top_aligned = paint_desc.text_top_aligned;
                cmd.text_inset = paint_desc.text_inset;
                if (cmd.kind == PrimitiveKind::GlyphRun) {
                    for (const auto& editor : editors_) {
                        if (editor.instance == inst_id && editor.text_index == paint_desc.text_index) {
                            cmd.text_scroll_x = editor.scroll_x;
                            cmd.text_scroll_y = editor.scroll_y;
                            break;
                        }
                    }
                }
                if (paint_desc.radio_dot) {
                    cmd.bounds = Rect{geom.x + 6.0f, geom.y + 6.0f, 6.0f, 6.0f};
                    cmd.radii = CornerRadii{3.0f};
                }

                // Dynamic text lookup from storage or fallback to program table
                if (paint_desc.text_index != ~0u && paint_desc.text_index < rec->text_count) {
                    cmd.text_str = storage_.GetText(inst_id, paint_desc.text_index);
                } else if (paint_desc.text_index < prog.initial_texts.size()) {
                    cmd.text_str = prog.initial_texts[paint_desc.text_index];
                }

                // Interactive hover and pressed styling
                if (inst_id == hovered_instance_ && paint_desc.geometry_slot == hovered_slot_) {
                    if (inst_id == pressed_instance_ && paint_desc.geometry_slot == pressed_slot_) {
                        cmd.fill_color = cmd.fill_color.Lighten(-0.06f);
                    } else {
                        cmd.fill_color = cmd.fill_color.Lighten(0.12f);
                        cmd.border_color = cmd.border_color.Lighten(0.20f);
                    }
                }
                if (inst_id == focused_instance_ && paint_desc.geometry_slot == focused_slot_ &&
                    cmd.kind != PrimitiveKind::GlyphRun) {
                    cmd.border_color = theme::Blue500;
                    cmd.border_width = (std::max)(cmd.border_width, 2.0f);
                }

                const bool focused_field = inst_id == focused_instance_ &&
                    paint_desc.geometry_slot == focused_slot_ &&
                    paint_desc.text_index == focused_text_index_ &&
                    paint_desc.kind == PrimitiveKind::GlyphRun;
                EditorEntry* edit_entry = focused_field ? FocusedEditorEntry() : nullptr;
                if (edit_entry) {
                    cmd.text_str = Utf16ToUtf8(edit_entry->model.DisplayText());
                    cmd.text_scroll_x = edit_entry->scroll_x;
                    cmd.text_scroll_y = edit_entry->scroll_y;
                    cmd.text_multiline = focused_multiline_;
                    if (edit_entry->model.HasSelection() && !edit_entry->model.IsComposing()) {
                        auto first = (std::min)(edit_entry->model.Caret(), edit_entry->model.Anchor());
                        auto last = (std::max)(edit_entry->model.Caret(), edit_entry->model.Anchor());
                        auto rects = DirectWriteTextEngine::Instance().SelectionRects(edit_entry->model.Text(),
                            static_cast<uint32_t>(first), static_cast<uint32_t>(last - first),
                            geom.width - 24.0f, focused_multiline_);
                        const float top = geom.y + (focused_multiline_ ? 8.0f : (geom.height - 18.0f) * 0.5f);
                        for (auto selected : rects) {
                            selected.x += geom.x + 12.0f - edit_entry->scroll_x;
                            selected.y += top - edit_entry->scroll_y;
                            const float left = (std::max)(selected.x, geom.x + 11.0f);
                            const float right = (std::min)(selected.Right(), geom.Right() - 10.0f);
                            const float selected_top = (std::max)(selected.y, geom.y + 3.0f);
                            const float bottom = (std::min)(selected.Bottom(), geom.Bottom() - 3.0f);
                            if (right > left && bottom > selected_top) {
                                DrawCommand highlight{};
                                highlight.kind = PrimitiveKind::Rect;
                                highlight.bounds = Rect{left, selected_top, right - left, bottom - selected_top};
                                highlight.fill_color = Color::FromRgba8(54, 112, 210, 180);
                                highlight.visual_order = cmd.visual_order;
                                packet.commands.push_back(highlight);
                            }
                        }
                    }
                }
                packet.commands.push_back(std::move(cmd));
                if (edit_entry) {
                    if (edit_entry->model.IsComposing() && edit_entry->model.CompositionLength()) {
                        const auto ranges = DirectWriteTextEngine::Instance().SelectionRects(
                            edit_entry->model.DisplayText(), static_cast<uint32_t>(edit_entry->model.CompositionStart()),
                            static_cast<uint32_t>(edit_entry->model.CompositionLength()), geom.width - 24, focused_multiline_);
                        for (auto range : ranges) {
                            const float x = geom.x + 12 + range.x - edit_entry->scroll_x;
                            const float y = geom.y + (focused_multiline_ ? 8 : (geom.height - 18) * 0.5f) +
                                range.y + range.height - 1 - edit_entry->scroll_y;
                            const auto bounds = Rect{x, y, range.width, 1.5f}.Intersect(
                                Rect{geom.x + 10, geom.y + 3, geom.width - 20, geom.height - 6});
                            if (bounds.width > 0 && bounds.height > 0) {
                                DrawCommand underline{};
                                underline.kind = PrimitiveKind::Rect;
                                underline.bounds = bounds;
                                underline.fill_color = theme::Blue500;
                                packet.commands.push_back(underline);
                            }
                        }
                    }
                    const auto elapsed = std::chrono::steady_clock::now() - focus_changed_at_;
                    const auto half_seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 500;
                    if (half_seconds % 2 == 0 || edit_entry->model.IsComposing()) {
                        const auto metrics = DirectWriteTextEngine::Instance().CaretAt(edit_entry->model.DisplayText(),
                            static_cast<uint32_t>(edit_entry->model.DisplayCaret()), geom.width - 24.0f,
                            focused_multiline_);
                        DrawCommand caret{};
                        caret.kind = PrimitiveKind::Rect;
                        caret.bounds = Rect{(std::clamp)(geom.x + 12.0f + metrics.x - edit_entry->scroll_x,
                            geom.x + 11.0f, geom.Right() - 11.0f),
                            geom.y + (focused_multiline_ ? 8.0f : (geom.height - 18.0f) * 0.5f) +
                                metrics.y - edit_entry->scroll_y,
                            1.5f, metrics.height};
                        caret.fill_color = theme::Blue500;
                        caret.visual_order = layer->SortKey() | (paint_desc.local_order + 1);
                        packet.commands.push_back(caret);
                    }
                }
            }
        }
    }

    return packet;
}

} // namespace arcade_ui
