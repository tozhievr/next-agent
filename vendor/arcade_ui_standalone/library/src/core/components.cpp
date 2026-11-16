#include <arcade_ui/core/components.hpp>
#include <arcade_ui/core/theme.hpp>
#include <arcade_ui/core/text_edit.hpp>
#include <arcade_ui/win32/text_engine.hpp>
#include <algorithm>
#include <cwctype>
#include <format>
#include <stdexcept>
#include <windows.h>

namespace arcade_ui::components {
namespace {

// Each element owns one geometry/text slot and two styles (surface, foreground).
class ProgramBuilder {
public:
    ComponentProgram program;
    explicit ProgramBuilder(std::string_view name) { program.name = name; Box(); }
    uint32_t Box(bool left = false, bool wrap = false) {
        const uint32_t slot = program.total_geometry_slots++;
        PaintStyleData background{};
        background.fill_color = theme::Zinc900;
        background.border_color = theme::Zinc700;
        background.border_width = 1;
        background.radii = CornerRadii{slot == 0 ? 10.0f : 6.0f};
        program.initial_styles.push_back(background);
        PaintStyleData text{};
        text.fill_color = theme::Zinc100;
        program.initial_styles.push_back(text);
        program.initial_texts.emplace_back();
        program.paint_descriptors.push_back(PaintDesc{.kind = PrimitiveKind::RoundedRect,
            .geometry_slot = SlotRef{slot}, .style_slot = SlotRef{slot * 2},
            .local_order = static_cast<uint32_t>(program.paint_descriptors.size())});
        program.paint_descriptors.push_back(PaintDesc{.kind = PrimitiveKind::GlyphRun,
            .geometry_slot = SlotRef{slot}, .style_slot = SlotRef{slot * 2 + 1},
            .local_order = static_cast<uint32_t>(program.paint_descriptors.size()),
            .text_index = slot, .text_left_aligned = left, .text_top_aligned = wrap});
        return slot;
    }
    template <typename Component, typename Fn>
    uint32_t Button(Fn fn, bool left = false) {
        const uint32_t slot = Box(left);
        const uint32_t handler = static_cast<uint32_t>(program.handlers.size());
        program.handlers.push_back([fn](void* state, void* raw) {
            auto& event = *static_cast<IEventContext*>(raw);
            fn(*static_cast<typename Component::State*>(state),
               *static_cast<const typename Component::Props*>(event.props));
        });
        program.input_descriptors.push_back(InputDesc{.geometry_slot = SlotRef{slot},
            .event_handler_id = handler,
            .flags = InputFlags::Focusable | InputFlags::Clickable | InputFlags::Hoverable,
            .local_order = static_cast<uint32_t>(program.input_descriptors.size())});
        return slot;
    }
    uint32_t Field() {
        const uint32_t slot = Box(true);
        program.initial_styles[slot * 2].fill_color = theme::Zinc950;
        program.input_descriptors.push_back(InputDesc{.geometry_slot = SlotRef{slot},
            .event_handler_id = ~0u, .flags = InputFlags::Focusable | InputFlags::Clickable,
            .text_index = slot});
        return slot;
    }
    template <typename Component>
    ComponentProgram Finish() {
        program.total_style_slots = static_cast<uint32_t>(program.initial_styles.size());
        program.total_text_slots = static_cast<uint32_t>(program.initial_texts.size());
        program.update_bindings = [](UiStorage& storage, InstanceId id, void* state, void* props) {
            Component::Bind(storage, id, *static_cast<typename Component::State*>(state),
                            *static_cast<const typename Component::Props*>(props));
        };
        if constexpr (requires { &Component::Key; }) {
            program.on_key = [](UiStorage& storage, InstanceId id, void* state, void* props,
                SlotRef slot, uint32_t key, bool ctrl, bool shift) {
                return Component::Key(storage, id, *static_cast<typename Component::State*>(state),
                    *static_cast<const typename Component::Props*>(props), slot, key, ctrl, shift);
            };
        }
        if constexpr (requires { &Component::Blur; }) {
            program.on_blur = [](void* state, void* props) {
                Component::Blur(*static_cast<typename Component::State*>(state),
                    *static_cast<const typename Component::Props*>(props));
            };
        }
        return std::move(program);
    }
};

void Place(UiStorage& storage, InstanceId id, uint32_t slot, float x, float y, float width, float height) {
    const auto origin = storage.GetRect(id, SlotRef{0}).TopLeft();
    storage.GetRect(id, SlotRef{slot}) = Rect{origin.x + x, origin.y + y, width, height};
}
void Hide(UiStorage& storage, InstanceId id, uint32_t slot) {
    storage.GetRect(id, SlotRef{slot}) = {};
}
void Paint(UiStorage& storage, InstanceId id, uint32_t slot, Color background,
           Color foreground = theme::Zinc100, bool border = true) {
    auto& bg = storage.GetStyle(id, SlotRef{slot * 2});
    bg.fill_color = background;
    bg.border_width = border ? 1.0f : 0.0f;
    storage.GetStyle(id, SlotRef{slot * 2 + 1}).fill_color = foreground;
}
void Enable(UiStorage& storage, InstanceId id, uint32_t slot, bool enabled) {
    auto* record = storage.GetInstance(id);
    for (uint32_t i = 0; i < record->input_count; ++i) {
        auto& entry = storage.GetInput(id, i);
        if (entry.geom_slot.local == slot) entry.enabled = enabled;
    }
}
std::wstring Fold(std::string_view value) {
    auto wide = Utf8ToUtf16(value);
    std::transform(wide.begin(), wide.end(), wide.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return wide;
}
void Choose(Combobox::State& state, const Combobox::Props& props) {
    if (state.filtered.empty()) return;
    state.selected = state.filtered[static_cast<size_t>(state.active)];
    state.expanded = false;
    if (props.on_change) props.on_change(state.selected);
}
void Page(Pagination::State& state, const Pagination::Props& props, int page) {
    const int next = std::clamp(page, 1, (std::max)(1, props.pages));
    if (state.page == next) return;
    state.page = next;
    if (props.on_change) props.on_change(next);
}
void SelectDate(Calendar::State& state, const Calendar::Props& props, std::chrono::year_month_day date) {
    if (!date.ok() || int(date.year()) < 1 || int(date.year()) > 9999) return;
    state.selected = date;
    state.month = date.year() / date.month();
    if (props.on_change) props.on_change(date);
}
void Month(Calendar::State& state, int delta) {
    auto next = state.month + std::chrono::months{delta};
    if (int(next.year()) >= 1 && int(next.year()) <= 9999) state.month = next;
}
}

ComponentProgram Combobox::BuildProgram() {
    ProgramBuilder builder("Combobox / Select");
    builder.Button<Combobox>([](State& state, const Props&) { state.expanded = !state.expanded; }, true);
    builder.Field();
    for (int row = 0; row < static_cast<int>(Capacity); ++row) {
        builder.Button<Combobox>([row](State& state, const Props& props) {
            if (row >= static_cast<int>(state.filtered.size())) return;
            state.active = row;
            Choose(state, props);
        }, true);
    }
    builder.Box();
    return builder.Finish<Combobox>();
}

void Combobox::Bind(UiStorage& storage, InstanceId id, State& state, const Props& props) {
    if (props.items.size() > Capacity) throw std::invalid_argument("Combobox supports up to 8 items");
    const std::string query = props.searchable ? storage.GetText(id, 2) : "";
    if (query != state.query) { state.query = query; state.active = 0; }
    state.filtered.clear();
    const auto needle = Fold(query);
    for (size_t i = 0; i < props.items.size(); ++i)
        if (Fold(props.items[i]).find(needle) != std::wstring::npos) state.filtered.push_back(static_cast<int>(i));
    state.active = std::clamp(state.active, 0, (std::max)(0, static_cast<int>(state.filtered.size()) - 1));
    auto& root = storage.GetRect(id, SlotRef{0});
    root.width = 300;
    root.height = 64;
    Place(storage, id, 1, 12, 12, 276, 40);
    storage.GetText(id, 1) = (state.selected >= 0 && state.selected < static_cast<int>(props.items.size()) ?
        props.items[static_cast<size_t>(state.selected)] : props.placeholder) + (state.expanded ? "   ∧" : "   ∨");
    float y = 60;
    if (state.expanded && props.searchable) { Place(storage, id, 2, 12, y, 276, 36); y += 44; }
    else Hide(storage, id, 2);
    Enable(storage, id, 2, state.expanded && props.searchable);
    for (size_t row = 0; row < Capacity; ++row) {
        const uint32_t slot = 3 + static_cast<uint32_t>(row);
        const bool visible = state.expanded && row < state.filtered.size();
        Enable(storage, id, slot, visible);
        if (!visible) { Hide(storage, id, slot); continue; }
        const int item = state.filtered[row];
        Place(storage, id, slot, 12, y, 276, 32); y += 36;
        storage.GetText(id, slot) = props.items[static_cast<size_t>(item)] + (state.selected == item ? "   ✓" : "");
        Paint(storage, id, slot, state.active == static_cast<int>(row) ? theme::Zinc700 : theme::Zinc900,
            theme::Zinc100, false);
    }
    if (state.expanded && state.filtered.empty()) {
        Place(storage, id, 11, 12, y, 276, 32); y += 36;
        storage.GetText(id, 11) = "No results found";
        Paint(storage, id, 11, Color::Transparent(), theme::Zinc400, false);
    } else Hide(storage, id, 11);
    if (state.expanded) root.height = y + 8;
}

bool Combobox::Key(UiStorage&, InstanceId, State& state, const Props& props, SlotRef slot,
                  uint32_t key, bool ctrl, bool) {
    if (ctrl) return false;
    if (key == VK_ESCAPE && state.expanded) { state.expanded = false; return true; }
    if (key == VK_DOWN || key == VK_UP) {
        if (!state.expanded) { state.expanded = true; state.active = 0; }
        else if (!state.filtered.empty()) state.active = (state.active + (key == VK_DOWN ? 1 :
            static_cast<int>(state.filtered.size()) - 1)) % static_cast<int>(state.filtered.size());
        return true;
    }
    if (key == VK_RETURN && state.expanded && slot.local <= 2) { Choose(state, props); return true; }
    return false;
}

ComponentProgram Accordion::BuildProgram() {
    ProgramBuilder builder("Accordion");
    for (uint32_t row = 0; row < Capacity; ++row) {
        builder.Button<Accordion>([row](State& state, const Props& props) {
            const uint32_t bit = 1u << row;
            state.expanded = props.multiple ? state.expanded ^ bit : (state.expanded == bit ? 0 : bit);
        }, true);
        builder.Box(true, true);
    }
    return builder.Finish<Accordion>();
}

void Accordion::Bind(UiStorage& storage, InstanceId id, State& state, const Props& props) {
    if (props.items.size() > Capacity) throw std::invalid_argument("Accordion supports up to 4 items");
    auto& root = storage.GetRect(id, SlotRef{0}); root.width = 300;
    float y = 12;
    for (uint32_t row = 0; row < Capacity; ++row) {
        const uint32_t title = 1 + row * 2, body = title + 1;
        if (row >= props.items.size()) { Hide(storage, id, title); Hide(storage, id, body); Enable(storage, id, title, false); continue; }
        Enable(storage, id, title, true);
        const bool open = (state.expanded & (1u << row)) != 0;
        Place(storage, id, title, 12, y, 276, 40); y += 44;
        storage.GetText(id, title) = props.items[row].title + (open ? "   −" : "   +");
        Paint(storage, id, title, Color::Transparent(), theme::Zinc100, false);
        if (open) {
            const float height = (std::max)(44.0f, std::ceil(DirectWriteTextEngine::Instance()
                .MeasureText(props.items[row].content, 14.0f, L"Segoe UI", 252.0f).height) + 16.0f);
            Place(storage, id, body, 12, y, 276, height); y += height + 4;
            storage.GetText(id, body) = props.items[row].content;
            Paint(storage, id, body, theme::Zinc950, theme::Zinc300, false);
        } else Hide(storage, id, body);
    }
    root.height = y + 8;
}

ComponentProgram Pagination::BuildProgram() {
    ProgramBuilder builder("Pagination");
    builder.Button<Pagination>([](State& state, const Props& props) { Page(state, props, state.page - 1); });
    for (int row = 0; row < 5; ++row)
        builder.Button<Pagination>([row](State& state, const Props& props) { Page(state, props, state.first + row); });
    builder.Button<Pagination>([](State& state, const Props& props) { Page(state, props, state.page + 1); });
    builder.Box();
    return builder.Finish<Pagination>();
}

void Pagination::Bind(UiStorage& storage, InstanceId id, State& state, const Props& props) {
    const int pages = (std::max)(1, props.pages);
    state.page = std::clamp(state.page, 1, pages);
    state.first = std::clamp(state.page - 2, 1, (std::max)(1, pages - 4));
    auto& root = storage.GetRect(id, SlotRef{0}); root.width = 300; root.height = 94;
    Place(storage, id, 1, 12, 12, 32, 32); storage.GetText(id, 1) = "‹";
    Place(storage, id, 7, 256, 12, 32, 32); storage.GetText(id, 7) = "›";
    Enable(storage, id, 1, state.page > 1); Enable(storage, id, 7, state.page < pages);
    Paint(storage, id, 1, theme::Zinc900, state.page > 1 ? theme::Zinc100 : theme::Zinc500);
    Paint(storage, id, 7, theme::Zinc900, state.page < pages ? theme::Zinc100 : theme::Zinc500);
    for (int row = 0; row < 5; ++row) {
        const uint32_t slot = 2 + row;
        const int page = state.first + row;
        const bool visible = page <= pages;
        Enable(storage, id, slot, visible);
        if (!visible) { Hide(storage, id, slot); continue; }
        Place(storage, id, slot, 51.0f + row * 40.0f, 12, 32, 32);
        storage.GetText(id, slot) = std::to_string(page);
        Paint(storage, id, slot, page == state.page ? theme::Zinc700 : theme::Zinc900);
    }
    Place(storage, id, 8, 12, 54, 276, 24);
    storage.GetText(id, 8) = std::format("Page {} of {}", state.page, pages);
    Paint(storage, id, 8, Color::Transparent(), theme::Zinc400, false);
}

bool Pagination::Key(UiStorage&, InstanceId, State& state, const Props& props, SlotRef,
                     uint32_t key, bool ctrl, bool) {
    if (ctrl || (key != VK_LEFT && key != VK_RIGHT && key != VK_HOME && key != VK_END)) return false;
    Page(state, props, key == VK_HOME ? 1 : key == VK_END ? props.pages : state.page + (key == VK_LEFT ? -1 : 1));
    return true;
}

ComponentProgram Calendar::BuildProgram() {
    ProgramBuilder builder("Calendar");
    builder.Box();
    builder.Button<Calendar>([](State& state, const Props&) { Month(state, -1); });
    builder.Button<Calendar>([](State& state, const Props&) { Month(state, 1); });
    for (int i = 0; i < 7; ++i) builder.Box();
    for (int i = 0; i < 42; ++i)
        builder.Button<Calendar>([i](State& state, const Props& props) { SelectDate(state, props, state.dates[i]); });
    builder.Box();
    return builder.Finish<Calendar>();
}

void Calendar::Bind(UiStorage& storage, InstanceId id, State& state, const Props& props) {
    if (!state.initialized) {
        if (!props.month.ok() || int(props.month.year()) < 1 || int(props.month.year()) > 9999)
            throw std::invalid_argument("Calendar month must be in years 1..9999");
        state.month = props.month; state.selected = props.selected; state.initialized = true;
    }
    auto& root = storage.GetRect(id, SlotRef{0}); root.width = 300; root.height = 342;
    Place(storage, id, 1, 48, 12, 204, 32);
    static constexpr const char* months[] = {"January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    storage.GetText(id, 1) = std::format("{} {}", months[unsigned(state.month.month()) - 1], int(state.month.year()));
    Paint(storage, id, 1, Color::Transparent(), theme::Zinc100, false);
    Place(storage, id, 2, 12, 12, 32, 32); storage.GetText(id, 2) = "‹";
    Place(storage, id, 3, 256, 12, 32, 32); storage.GetText(id, 3) = "›";
    Enable(storage, id, 2, true); Enable(storage, id, 3, true);
    static constexpr const char* weekdays[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    for (uint32_t day = 0; day < 7; ++day) {
        Place(storage, id, 4 + day, 13.0f + day * 39.0f, 52, 36, 24);
        storage.GetText(id, 4 + day) = weekdays[day];
        Paint(storage, id, 4 + day, Color::Transparent(), theme::Zinc400, false);
    }
    const auto first = std::chrono::sys_days{state.month / 1};
    const auto start = first - std::chrono::days{std::chrono::weekday{first}.iso_encoding() - 1};
    for (uint32_t cell = 0; cell < 42; ++cell) {
        const auto date = std::chrono::year_month_day{start + std::chrono::days{cell}};
        state.dates[cell] = date;
        const uint32_t slot = 11 + cell;
        Place(storage, id, slot, 13.0f + (cell % 7) * 39.0f, 80.0f + (cell / 7) * 36.0f, 36, 32);
        storage.GetText(id, slot) = std::to_string(unsigned(date.day()));
        const bool selected = state.selected && *state.selected == date;
        if (selected && state.focus_selected) storage.GetInstance(id)->requested_focus = SlotRef{slot};
        Paint(storage, id, slot, selected ? theme::Zinc100 : Color::Transparent(),
            selected ? theme::Zinc950 : date.month() == state.month.month() ? theme::Zinc100 : theme::Zinc500, false);
        Enable(storage, id, slot, int(date.year()) >= 1 && int(date.year()) <= 9999);
    }
    Place(storage, id, 53, 12, 306, 276, 24);
    storage.GetText(id, 53) = state.selected ? std::format("Selected: {:04}-{:02}-{:02}",
        int(state.selected->year()), unsigned(state.selected->month()), unsigned(state.selected->day())) : "Choose a date";
    Paint(storage, id, 53, Color::Transparent(), theme::Zinc400, false);
    state.focus_selected = false;
}

bool Calendar::Key(UiStorage&, InstanceId, State& state, const Props& props, SlotRef,
                   uint32_t key, bool ctrl, bool) {
    if (ctrl) return false;
    if (key == VK_PRIOR || key == VK_NEXT) { Month(state, key == VK_PRIOR ? -1 : 1); return true; }
    int offset = key == VK_LEFT ? -1 : key == VK_RIGHT ? 1 : key == VK_UP ? -7 : key == VK_DOWN ? 7 : 0;
    if (!offset) return false;
    auto date = state.selected.value_or(std::chrono::year_month_day{state.month / 1});
    SelectDate(state, props, std::chrono::year_month_day{std::chrono::sys_days{date} + std::chrono::days{offset}});
    state.focus_selected = true;
    return true;
}

ComponentProgram ToggleGroup::BuildProgram() {
    ProgramBuilder builder("Toggle Group / Button Group");
    for (uint32_t row = 0; row < Capacity; ++row)
        builder.Button<ToggleGroup>([row](State& state, const Props& props) {
            state.selected = props.multiple ? state.selected ^ (1u << row) : 1u << row;
            if (props.on_change) props.on_change(state.selected);
        });
    return builder.Finish<ToggleGroup>();
}

void ToggleGroup::Bind(UiStorage& storage, InstanceId id, State& state, const Props& props) {
    if (props.items.size() > Capacity) throw std::invalid_argument("ToggleGroup supports up to 5 items");
    if (!state.initialized) { state.selected = props.initial_selection; state.initialized = true; }
    const uint32_t valid = (1u << props.items.size()) - 1;
    state.selected &= valid;
    if (!props.multiple && state.selected) state.selected &= ~state.selected + 1;
    auto& root = storage.GetRect(id, SlotRef{0}); root.width = 300; root.height = 60;
    const float width = 276.0f / static_cast<float>((std::max)(size_t{1}, props.items.size()));
    for (uint32_t row = 0; row < Capacity; ++row) {
        Enable(storage, id, 1 + row, row < props.items.size());
        if (row >= props.items.size()) { Hide(storage, id, 1 + row); continue; }
        Place(storage, id, 1 + row, 12 + row * width, 12, width - 2, 36);
        storage.GetText(id, 1 + row) = props.items[row];
        Paint(storage, id, 1 + row, state.selected & (1u << row) ? theme::Zinc700 : theme::Zinc900,
            theme::Zinc100, false);
    }
}

bool ToggleGroup::Key(UiStorage& storage, InstanceId id, State& state, const Props& props, SlotRef slot,
                      uint32_t key, bool ctrl, bool) {
    if (ctrl || props.items.empty() || (key != VK_LEFT && key != VK_RIGHT)) return false;
    const uint32_t current = slot.local > 0 ? slot.local - 1 : 0;
    const uint32_t count = static_cast<uint32_t>(props.items.size());
    const uint32_t next = (current + (key == VK_RIGHT ? 1 : count - 1)) % count;
    storage.GetInstance(id)->requested_focus = SlotRef{next + 1};
    if (!props.multiple) {
        state.selected = 1u << next;
        if (props.on_change) props.on_change(state.selected);
    }
    return true;
}

ComponentProgram DatePicker::BuildProgram() {
    ProgramBuilder builder("Date Picker");
    builder.program = Calendar::BuildProgram();
    builder.program.name = "Date Picker";
    for (size_t index = 0; index < builder.program.handlers.size(); ++index) {
        auto calendar_handler = std::move(builder.program.handlers[index]);
        builder.program.handlers[index] = [calendar_handler, index](void* raw_state, void* raw_event) {
            auto& state = *static_cast<State*>(raw_state);
            const auto& event = *static_cast<IEventContext*>(raw_event);
            const auto& props = *static_cast<const Props*>(event.props);
            auto forwarded = event;
            forwarded.state = &state.calendar;
            forwarded.props = const_cast<Calendar::Props*>(&props.calendar);
            calendar_handler(&state.calendar, &forwarded);
            if (index >= 2) state.expanded = false;
        };
    }
    builder.Button<DatePicker>([](State& state, const Props&) { state.expanded = !state.expanded; }, true);
    return builder.Finish<DatePicker>();
}

void DatePicker::Bind(UiStorage& storage, InstanceId id, State& state, const Props& props) {
    const bool focus_first = state.expanded && state.calendar.focus_selected && !state.calendar.selected;
    Calendar::Bind(storage, id, state.calendar, props.calendar);
    if (focus_first) {
        for (uint32_t cell = 0; cell < 42; ++cell) {
            if (state.calendar.dates[cell] == std::chrono::year_month_day{state.calendar.month / 1}) {
                storage.GetInstance(id)->requested_focus = SlotRef{11 + cell};
                break;
            }
        }
    }
    auto& root = storage.GetRect(id, SlotRef{0});
    root.height = state.expanded ? 394.0f : 60.0f;
    for (uint32_t slot = 1; slot <= 53; ++slot) {
        if (state.expanded) storage.GetRect(id, SlotRef{slot}).y += 52;
        else Hide(storage, id, slot);
    }
    const auto* record = storage.GetInstance(id);
    for (uint32_t input = 0; input < record->input_count; ++input) {
        auto& entry = storage.GetInput(id, input);
        if (entry.geom_slot.local != 54) entry.enabled = entry.enabled && state.expanded;
    }
    Place(storage, id, 54, 12, 12, 276, 36);
    storage.GetText(id, 54) = state.calendar.selected ? std::format("{:04}-{:02}-{:02}   ∨",
        int(state.calendar.selected->year()), unsigned(state.calendar.selected->month()),
        unsigned(state.calendar.selected->day())) : props.placeholder + "   ∨";
}

bool DatePicker::Key(UiStorage& storage, InstanceId id, State& state, const Props& props, SlotRef slot,
                     uint32_t key, bool ctrl, bool shift) {
    if (key == VK_ESCAPE && state.expanded) { state.expanded = false; return true; }
    if (slot.local == 54 && key == VK_DOWN && !ctrl) {
        state.expanded = true; state.calendar.focus_selected = true; return true;
    }
    if (!state.expanded) return false;
    return Calendar::Key(storage, id, state.calendar, props.calendar, slot, key, ctrl, shift);
}

ComponentProgram RadioGroup::BuildProgram() {
    ProgramBuilder builder("Radio Group");
    for (uint32_t row = 0; row < Capacity; ++row) {
        const uint32_t label = builder.Button<RadioGroup>([row](State& state, const Props& props) {
            if (row >= props.items.size() || props.items[row].disabled || state.selected == static_cast<int>(row)) return;
            state.selected = static_cast<int>(row);
            if (props.on_change) props.on_change(state.selected);
        }, true);
        builder.program.paint_descriptors.back().text_inset = 44;
        const uint32_t ring = builder.Box();
        const uint32_t dot = builder.Box();
        builder.program.initial_styles[ring * 2].radii = CornerRadii{9};
        builder.program.initial_styles[dot * 2].radii = CornerRadii{3};
        (void)label;
    }
    return builder.Finish<RadioGroup>();
}

void RadioGroup::Bind(UiStorage& storage, InstanceId id, State& state, const Props& props) {
    if (props.items.size() > Capacity) throw std::invalid_argument("RadioGroup supports up to 5 items");
    if (!state.initialized) { state.selected = props.initial_selection; state.initialized = true; }
    if (state.selected < 0 || state.selected >= static_cast<int>(props.items.size()) || props.items[state.selected].disabled) {
        state.selected = -1;
        for (size_t row = 0; row < props.items.size(); ++row)
            if (!props.items[row].disabled) { state.selected = static_cast<int>(row); break; }
    }
    auto& root = storage.GetRect(id, SlotRef{0}); root.width = 300; root.height = 24 + props.items.size() * 40.0f;
    for (uint32_t row = 0; row < Capacity; ++row) {
        const uint32_t label = 1 + row * 3, ring = label + 1, dot = label + 2;
        const bool exists = row < props.items.size();
        Enable(storage, id, label, exists && !props.items[row].disabled);
        if (!exists) { Hide(storage, id, label); Hide(storage, id, ring); Hide(storage, id, dot); continue; }
        const bool selected = state.selected == static_cast<int>(row);
        const Color foreground = props.items[row].disabled ? theme::Zinc500 : theme::Zinc100;
        const float y = 12 + row * 40.0f;
        Place(storage, id, label, 12, y, 276, 36);
        storage.GetText(id, label) = props.items[row].label;
        Paint(storage, id, label, Color::Transparent(), foreground, false);
        Place(storage, id, ring, 24, y + 9, 18, 18);
        Paint(storage, id, ring, Color::Transparent(), foreground);
        auto& ring_style = storage.GetStyle(id, SlotRef{ring * 2});
        ring_style.border_color = props.items[row].disabled ? theme::Zinc700 : selected ? theme::Zinc100 : theme::Zinc500;
        ring_style.border_width = 1.5f;
        Place(storage, id, dot, 30, y + 15, 6, 6);
        Paint(storage, id, dot, selected ? foreground : Color::Transparent(), foreground, false);
    }
}

bool RadioGroup::Key(UiStorage& storage, InstanceId id, State& state, const Props& props, SlotRef slot,
                     uint32_t key, bool ctrl, bool) {
    if (ctrl || props.items.empty()) return false;
    const int count = static_cast<int>(props.items.size());
    const bool forward = key == VK_DOWN || key == VK_RIGHT || key == VK_HOME;
    if (!forward && key != VK_UP && key != VK_LEFT && key != VK_END) return false;
    int current = key == VK_HOME ? count - 1 : key == VK_END ? 0 : static_cast<int>((slot.local - 1) / 3);
    for (int attempt = 0; attempt < count; ++attempt) {
        current = (current + (forward ? 1 : count - 1)) % count;
        if (!props.items[current].disabled) {
            const bool changed = state.selected != current;
            state.selected = current;
            storage.GetInstance(id)->requested_focus = SlotRef{1 + static_cast<uint32_t>(current) * 3};
            if (changed && props.on_change) props.on_change(current);
            break;
        }
    }
    return true;
}

ComponentProgram Breadcrumb::BuildProgram() {
    ProgramBuilder builder("Breadcrumb");
    for (uint32_t row = 0; row < Capacity; ++row) {
        builder.Button<Breadcrumb>([row](State& state, const Props& props) {
            state.current = static_cast<int>(row);
            if (props.on_navigate) props.on_navigate(state.current);
        });
        builder.Box();
    }
    return builder.Finish<Breadcrumb>();
}

void Breadcrumb::Bind(UiStorage& storage, InstanceId id, State& state, const Props& props) {
    if (props.items.size() > Capacity) throw std::invalid_argument("Breadcrumb supports up to 4 items");
    if (!state.initialized) { state.current = static_cast<int>(props.items.size()) - 1; state.initialized = true; }
    state.current = std::clamp(state.current, -1, static_cast<int>(props.items.size()) - 1);
    auto& root = storage.GetRect(id, SlotRef{0}); root.height = 56;
    float x = 12;
    for (uint32_t row = 0; row < Capacity; ++row) {
        const uint32_t label = 1 + row * 2, separator = label + 1;
        const bool exists = static_cast<int>(row) <= state.current;
        const bool current = static_cast<int>(row) == state.current;
        Enable(storage, id, label, exists && !current);
        if (!exists) { Hide(storage, id, label); Hide(storage, id, separator); continue; }
        const float width = std::ceil(DirectWriteTextEngine::Instance().MeasureText(props.items[row], 14).width) + 24;
        Place(storage, id, label, x, 12, width, 32); x += width;
        storage.GetText(id, label) = props.items[row];
        Paint(storage, id, label, Color::Transparent(), current ? theme::Zinc100 : theme::Zinc400, false);
        if (!current) {
            Place(storage, id, separator, x, 12, 20, 32); x += 20;
            storage.GetText(id, separator) = "›";
            Paint(storage, id, separator, Color::Transparent(), theme::Zinc500, false);
        } else Hide(storage, id, separator);
    }
    root.width = x + 12;
}

bool Breadcrumb::Key(UiStorage& storage, InstanceId id, State& state, const Props&, SlotRef slot,
                     uint32_t key, bool ctrl, bool) {
    if (ctrl || state.current < 1 || (key != VK_LEFT && key != VK_RIGHT)) return false;
    const int current = static_cast<int>((slot.local - 1) / 2);
    const int next = (current + (key == VK_RIGHT ? 1 : state.current - 1)) % state.current;
    storage.GetInstance(id)->requested_focus = SlotRef{1 + static_cast<uint32_t>(next) * 2};
    return true;
}

} // namespace arcade_ui::components
