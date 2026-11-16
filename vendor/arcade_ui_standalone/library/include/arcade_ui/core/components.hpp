#pragma once

#include "host.hpp"
#include <array>
#include <chrono>
#include <functional>

namespace arcade_ui::components {

// Compound native controls. Mount with CompileComponent<T>() and UiHost::Mount<T>().
// Capacities are explicit; extra items are rejected rather than silently dropped.
struct Combobox {
    static constexpr size_t Capacity = 8;
    struct Props {
        std::vector<std::string> items;
        std::string placeholder = "Select an option";
        bool searchable = true;
        std::function<void(int)> on_change;
    };
    struct State {
        int selected = -1;
        int active = 0;
        bool expanded = false;
        std::vector<int> filtered;
        std::string query;
    };
    static ComponentProgram BuildProgram();
    static void Bind(UiStorage&, InstanceId, State&, const Props&);
    static bool Key(UiStorage&, InstanceId, State&, const Props&, SlotRef, uint32_t, bool, bool);
    static void Blur(State& state, const Props&) { state.expanded = false; }
};
// Set Props::searchable=false to render the Select variant.
using Select = Combobox;

struct Accordion {
    static constexpr size_t Capacity = 4;
    struct Item { std::string title; std::string content; };
    struct Props { std::vector<Item> items; bool multiple = false; };
    struct State { uint32_t expanded = 0; };
    static ComponentProgram BuildProgram();
    static void Bind(UiStorage&, InstanceId, State&, const Props&);
};

struct Pagination {
    struct Props { int pages = 1; std::function<void(int)> on_change; };
    struct State { int page = 1; int first = 1; };
    static ComponentProgram BuildProgram();
    static void Bind(UiStorage&, InstanceId, State&, const Props&);
    static bool Key(UiStorage&, InstanceId, State&, const Props&, SlotRef, uint32_t, bool, bool);
};

struct Calendar {
    struct Props {
        std::chrono::year_month month = [] {
            auto today = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
            return today.year() / today.month();
        }();
        std::optional<std::chrono::year_month_day> selected;
        std::function<void(std::chrono::year_month_day)> on_change;
    };
    struct State {
        bool initialized = false;
        bool focus_selected = false;
        std::chrono::year_month month;
        std::optional<std::chrono::year_month_day> selected;
        std::array<std::chrono::year_month_day, 42> dates{};
    };
    static ComponentProgram BuildProgram();
    static void Bind(UiStorage&, InstanceId, State&, const Props&);
    static bool Key(UiStorage&, InstanceId, State&, const Props&, SlotRef, uint32_t, bool, bool);
};

struct ToggleGroup {
    static constexpr size_t Capacity = 5;
    struct Props {
        std::vector<std::string> items;
        bool multiple = false;
        uint32_t initial_selection = 1;
        std::function<void(uint32_t)> on_change;
    };
    struct State { bool initialized = false; uint32_t selected = 0; };
    static ComponentProgram BuildProgram();
    static void Bind(UiStorage&, InstanceId, State&, const Props&);
    static bool Key(UiStorage&, InstanceId, State&, const Props&, SlotRef, uint32_t, bool, bool);
};

struct DatePicker {
    struct Props { Calendar::Props calendar; std::string placeholder = "Pick a date"; };
    struct State { Calendar::State calendar; bool expanded = false; };
    static ComponentProgram BuildProgram();
    static void Bind(UiStorage&, InstanceId, State&, const Props&);
    static bool Key(UiStorage&, InstanceId, State&, const Props&, SlotRef, uint32_t, bool, bool);
    static void Blur(State& state, const Props&) { state.expanded = false; }
};

struct RadioGroup {
    static constexpr size_t Capacity = 5;
    struct Item { std::string label; bool disabled = false; };
    struct Props {
        std::vector<Item> items;
        int initial_selection = 0;
        std::function<void(int)> on_change;
    };
    struct State { bool initialized = false; int selected = -1; };
    static ComponentProgram BuildProgram();
    static void Bind(UiStorage&, InstanceId, State&, const Props&);
    static bool Key(UiStorage&, InstanceId, State&, const Props&, SlotRef, uint32_t, bool, bool);
};

struct Breadcrumb {
    static constexpr size_t Capacity = 4;
    struct Props { std::vector<std::string> items; std::function<void(int)> on_navigate; };
    struct State { bool initialized = false; int current = -1; };
    static ComponentProgram BuildProgram();
    static void Bind(UiStorage&, InstanceId, State&, const Props&);
    static bool Key(UiStorage&, InstanceId, State&, const Props&, SlotRef, uint32_t, bool, bool);
};

} // namespace arcade_ui::components
