// arcade_ui/core/transaction.hpp
//
// Reactive state update transactions, binding propagation, and commit engine (§4.3, §9).
#pragma once

#include <vector>
#include <functional>
#include <unordered_map>
#include "types.hpp"
#include "ir.hpp"
#include "storage.hpp"

namespace arcade_ui {

class TransactionManager {
public:
    explicit TransactionManager(UiStorage& storage, DirtyStore& dirty_store)
        : storage_(storage), dirty_store_(dirty_store) {}

    // Apply a state mutation callback inside an active transaction
    template <typename State, typename Props>
    void MutateState(InstanceId id, std::function<void(State&, const Props&)> fn) {
        auto* rec = storage_.GetInstance(id);
        if (!rec || !rec->state || !rec->props) return;

        auto* s = static_cast<State*>(rec->state.get());
        const auto* p = static_cast<const Props*>(rec->props.get());

        // Execute mutation
        fn(*s, *p);

        // Mark instance as dirty for bindings and layout
        dirty_store_.MarkDirty(id, DirtyMask::Bindings | DirtyMask::Measure | DirtyMask::Arrange | DirtyMask::Paint);
    }

    // Commit all pending dirty changes across instances
    void Commit(const std::vector<ComponentProgram>& programs) {
        if (!dirty_store_.HasDirty()) return;

        auto entries = dirty_store_.Drain();
        for (const auto& entry : entries) {
            auto* rec = storage_.GetInstance(entry.id);
            if (!rec || rec->program_id >= programs.size()) continue;

            const auto& prog = programs[rec->program_id];

            // Evaluate bindings affected by state/prop changes
            if (HasFlag(entry.mask, DirtyMask::Bindings)) {
                for (const auto& b : prog.bindings) {
                    if (b.evaluator_id < prog.evaluators.size()) {
                        void* inst_data = rec->state.get();
                        void* dest_data = nullptr; // in slot memory or string
                        prog.evaluators[b.evaluator_id](inst_data, dest_data);
                    }
                }
            }

            // Run layout if marked
            if (HasFlag(entry.mask, DirtyMask::Measure) || HasFlag(entry.mask, DirtyMask::Arrange)) {
                // Layout execution happens in Host with actual viewport constraints
            }
        }
    }

private:
    UiStorage& storage_;
    DirtyStore& dirty_store_;
};

// Typed EventContext passed to component event handlers (§4.2)
template <typename Component>
class EventContext {
public:
    using State = typename Component::State;
    using Props = typename Component::Props;

    EventContext(InstanceId instance, TransactionManager& tx, float pointer_ratio = 0.0f)
        : instance_(instance), tx_(tx), pointer_ratio_(pointer_ratio) {}

    template <typename F>
    void update(F&& fn) {
        tx_.MutateState<State, Props>(instance_, std::forward<F>(fn));
    }

    [[nodiscard]] InstanceId GetInstance() const noexcept { return instance_; }
    [[nodiscard]] float PointerRatio() const noexcept { return pointer_ratio_; }

private:
    InstanceId instance_;
    TransactionManager& tx_;
    float pointer_ratio_ = 0.0f;
};

} // namespace arcade_ui
