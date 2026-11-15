// arcade_ui/core/storage.hpp
//
// Flat runtime data-oriented storage structures (§6.1).
// Generation-indexed handles, contiguous arrays, no tree pointers.
#pragma once

#include <vector>
#include <memory>
#include <span>
#include <cassert>
#include "types.hpp"
#include "ir.hpp"

namespace arcade_ui {

// --- Instance Record (§6.1) -----------------------------------------------
struct InstanceRecord {
    uint32_t program_id = 0;
    uint32_t generation = 0;
    Handle owner;
    LayerId layer;

    // Slot base offsets inside storage
    uint32_t geometry_base = 0;
    uint32_t geometry_count = 0;

    uint32_t style_base = 0;
    uint32_t style_count = 0;

    uint32_t input_base = 0;
    uint32_t input_count = 0;

    uint32_t text_base = 0;
    uint32_t text_count = 0;

    // Type-erased state and props memory
    std::shared_ptr<void> state;
    std::shared_ptr<void> props;

    bool mounted = false;
    SlotRef requested_focus;
};

// --- Storage for flat instances, geometry, styles, and inputs -------------
class UiStorage {
public:
    UiStorage() = default;

    // Allocate an instance and reserve slot ranges
    InstanceId AllocateInstance(uint32_t program_id, LayerId layer,
                                uint32_t geom_count, uint32_t style_count, uint32_t input_count,
                                uint32_t text_count = 0) {
        uint32_t idx = 0;
        if (!free_instance_indices_.empty()) {
            idx = free_instance_indices_.back();
            free_instance_indices_.pop_back();
        } else {
            idx = static_cast<uint32_t>(instances_.size());
            instances_.emplace_back();
        }

        InstanceRecord& rec = instances_[idx];
        rec.program_id = program_id;
        rec.generation = ++next_generation_;
        rec.layer = layer;
        rec.mounted = true;
        rec.requested_focus = {};

        // Geometry slots
        rec.geometry_base = static_cast<uint32_t>(geometry_rects_.size());
        rec.geometry_count = geom_count;
        geometry_rects_.resize(rec.geometry_base + geom_count, Rect{});

        // Style slots
        rec.style_base = static_cast<uint32_t>(styles_.size());
        rec.style_count = style_count;
        styles_.resize(rec.style_base + style_count, PaintStyleData{});

        // Input slots
        rec.input_base = static_cast<uint32_t>(input_entries_.size());
        rec.input_count = input_count;
        input_entries_.resize(rec.input_base + input_count);

        // Text slots
        rec.text_base = static_cast<uint32_t>(texts_.size());
        rec.text_count = text_count;
        texts_.resize(rec.text_base + text_count);

        return InstanceId{idx, rec.generation};
    }

    void FreeInstance(InstanceId id) {
        if (!IsAlive(id)) return;
        InstanceRecord& rec = instances_[id.index];
        rec.mounted = false;
        rec.state.reset();
        rec.props.reset();
        rec.generation = 0; // invalidate handles
        free_instance_indices_.push_back(id.index);
    }

    bool IsAlive(InstanceId id) const noexcept {
        return id.index < instances_.size() &&
               instances_[id.index].generation == id.generation &&
               instances_[id.index].mounted;
    }

    InstanceRecord* GetInstance(InstanceId id) noexcept {
        return IsAlive(id) ? &instances_[id.index] : nullptr;
    }
    const InstanceRecord* GetInstance(InstanceId id) const noexcept {
        return IsAlive(id) ? &instances_[id.index] : nullptr;
    }

    // Geometry access
    Rect& GetRect(InstanceId id, SlotRef slot) noexcept {
        const auto* rec = GetInstance(id);
        assert(rec && slot.local < rec->geometry_count);
        return geometry_rects_[rec->geometry_base + slot.local];
    }
    const Rect& GetRect(InstanceId id, SlotRef slot) const noexcept {
        const auto* rec = GetInstance(id);
        assert(rec && slot.local < rec->geometry_count);
        return geometry_rects_[rec->geometry_base + slot.local];
    }

    // Style access
    PaintStyleData& GetStyle(InstanceId id, SlotRef slot) noexcept {
        const auto* rec = GetInstance(id);
        assert(rec && slot.local < rec->style_count);
        return styles_[rec->style_base + slot.local];
    }
    const PaintStyleData& GetStyle(InstanceId id, SlotRef slot) const noexcept {
        const auto* rec = GetInstance(id);
        assert(rec && slot.local < rec->style_count);
        return styles_[rec->style_base + slot.local];
    }

    // Input target record
    struct InputEntry {
        InstanceId instance;
        SlotRef geom_slot;
        uint32_t handler_id = 0;
        InputFlags flags = InputFlags::None;
        uint32_t local_order = 0;
        uint32_t text_index = ~0u;
        bool multiline = false;
        bool enabled = true;
    };

    InputEntry& GetInput(InstanceId id, uint32_t local_idx) noexcept {
        const auto* rec = GetInstance(id);
        assert(rec && local_idx < rec->input_count);
        return input_entries_[rec->input_base + local_idx];
    }
    const InputEntry& GetInput(InstanceId id, uint32_t local_idx) const noexcept {
        const auto* rec = GetInstance(id);
        assert(rec && local_idx < rec->input_count);
        return input_entries_[rec->input_base + local_idx];
    }

    std::span<const InputEntry> AllInputs() const noexcept { return input_entries_; }

    // Text access
    std::string& GetText(InstanceId id, uint32_t local_idx) noexcept {
        const auto* rec = GetInstance(id);
        assert(rec && local_idx < rec->text_count);
        return texts_[rec->text_base + local_idx];
    }
    const std::string& GetText(InstanceId id, uint32_t local_idx) const noexcept {
        const auto* rec = GetInstance(id);
        assert(rec && local_idx < rec->text_count);
        return texts_[rec->text_base + local_idx];
    }

    void Clear() {
        instances_.clear();
        free_instance_indices_.clear();
        geometry_rects_.clear();
        styles_.clear();
        input_entries_.clear();
        texts_.clear();
    }

private:
    std::vector<InstanceRecord> instances_;
    std::vector<uint32_t> free_instance_indices_;

    std::vector<Rect> geometry_rects_;
    std::vector<PaintStyleData> styles_;
    std::vector<InputEntry> input_entries_;
    std::vector<std::string> texts_;

    uint32_t next_generation_ = 1;
};

// --- Dirty Store (§6.1, §9.1) ----------------------------------------------
class DirtyStore {
public:
    void MarkDirty(InstanceId id, DirtyMask mask) {
        if (!id.IsValid()) return;
        dirty_instances_.push_back({id, mask});
    }

    bool HasDirty() const noexcept { return !dirty_instances_.empty(); }

    struct Entry {
        InstanceId id;
        DirtyMask mask = DirtyMask::None;
    };

    std::vector<Entry> Drain() {
        std::vector<Entry> res = std::move(dirty_instances_);
        dirty_instances_.clear();
        return res;
    }

private:
    std::vector<Entry> dirty_instances_;
};

} // namespace arcade_ui
