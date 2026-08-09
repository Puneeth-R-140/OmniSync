#pragma once

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <istream>
#include <unordered_map>
#include <vector>

namespace omnisync::core {

/**
 * @brief Vector clock used for causal knowledge and safe distributed GC.
 *
 * Values are per-client contiguous operation sequence numbers, not physical
 * time and not HLC timestamps. A missing client is defined as having value 0.
 */
class VectorClock {
public:
    enum class Relation { Before, Equal, After, Concurrent };

private:
    std::unordered_map<uint64_t, uint64_t> clock_;
    uint64_t my_id_ = 0;

public:
    VectorClock() = default;

    explicit VectorClock(uint64_t client_id) : my_id_(client_id) {
        if (client_id != 0) clock_.emplace(client_id, 0);
    }

    uint64_t get(uint64_t client_id) const noexcept {
        const auto it = clock_.find(client_id);
        return it == clock_.end() ? 0 : it->second;
    }

    void tick() {
        if (my_id_ == 0) return;
        auto& value = clock_[my_id_];
        if (value == std::numeric_limits<uint64_t>::max()) {
            throw std::overflow_error("VectorClock sequence overflow");
        }
        ++value;
    }

    void update(uint64_t client_id, uint64_t sequence) {
        if (client_id == 0 || sequence == 0) return;
        auto& value = clock_[client_id];
        value = std::max(value, sequence);
    }

    void merge(const VectorClock& other) {
        for (const auto& [id, value] : other.clock_) update(id, value);
    }

    Relation relation(const VectorClock& other) const noexcept {
        bool less = false;
        bool greater = false;

        for (const auto& [id, value] : clock_) {
            const uint64_t other_value = other.get(id);
            less |= value < other_value;
            greater |= value > other_value;
            if (less && greater) return Relation::Concurrent;
        }
        for (const auto& [id, value] : other.clock_) {
            if (clock_.find(id) == clock_.end() && value > 0) {
                less = true;
                if (greater) return Relation::Concurrent;
            }
        }

        if (less) return Relation::Before;
        if (greater) return Relation::After;
        return Relation::Equal;
    }

    int compare(const VectorClock& other) const noexcept {
        switch (relation(other)) {
            case Relation::Before: return -1;
            case Relation::After: return 1;
            case Relation::Equal:
            case Relation::Concurrent: return 0;
        }
        return 0;
    }

    bool operator<(const VectorClock& other) const noexcept {
        return relation(other) == Relation::Before;
    }

    bool operator==(const VectorClock& other) const noexcept {
        return relation(other) == Relation::Equal;
    }

    bool operator!=(const VectorClock& other) const noexcept { return !(*this == other); }

    bool isConcurrent(const VectorClock& other) const noexcept {
        return relation(other) == Relation::Concurrent;
    }

    bool isEqual(const VectorClock& other) const noexcept {
        return relation(other) == Relation::Equal;
    }

    uint64_t getMinTime() const noexcept {
        if (clock_.empty()) return 0;
        uint64_t result = std::numeric_limits<uint64_t>::max();
        for (const auto& [_, value] : clock_) result = std::min(result, value);
        return result;
    }

    static VectorClock computeMinimum(const std::vector<VectorClock>& clocks) {
        if (clocks.empty()) return VectorClock{};

        VectorClock result = clocks.front();
        std::unordered_map<uint64_t, bool> ids;
        for (const auto& vc : clocks) {
            for (const auto& [id, _] : vc.clock_) ids[id] = true;
        }

        result.clock_.clear();
        for (const auto& [id, _] : ids) {
            uint64_t minimum = std::numeric_limits<uint64_t>::max();
            for (const auto& vc : clocks) minimum = std::min(minimum, vc.get(id));
            if (minimum > 0) result.clock_[id] = minimum;
        }
        return result;
    }

    void save(std::ostream& out) const {
        // Deterministic wire representation: sort IDs before writing.
        std::vector<std::pair<uint64_t, uint64_t>> entries(clock_.begin(), clock_.end());
        std::sort(entries.begin(), entries.end());
        const uint64_t count = static_cast<uint64_t>(entries.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto& [id, value] : entries) {
            out.write(reinterpret_cast<const char*>(&id), sizeof(id));
            out.write(reinterpret_cast<const char*>(&value), sizeof(value));
        }
    }

    bool load(std::istream& in, uint64_t max_entries = 1'000'000) {
        uint64_t count = 0;
        if (!in.read(reinterpret_cast<char*>(&count), sizeof(count))) return false;
        if (count > max_entries) return false;

        std::unordered_map<uint64_t, uint64_t> loaded;
        loaded.reserve(static_cast<std::size_t>(count));
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t id = 0;
            uint64_t value = 0;
            if (!in.read(reinterpret_cast<char*>(&id), sizeof(id)) ||
                !in.read(reinterpret_cast<char*>(&value), sizeof(value)) || id == 0) {
                return false;
            }
            auto [it, inserted] = loaded.emplace(id, value);
            if (!inserted && it->second != value) return false;
        }
        clock_ = std::move(loaded);
        if (my_id_ != 0) clock_.try_emplace(my_id_, 0);
        return true;
    }

    void print() const {
        std::vector<std::pair<uint64_t, uint64_t>> entries(clock_.begin(), clock_.end());
        std::sort(entries.begin(), entries.end());
        std::cout << "[";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << entries[i].first << ":" << entries[i].second;
        }
        std::cout << "]";
    }

    static VectorClock fromState(
        uint64_t client_id,
        const std::unordered_map<uint64_t, uint64_t>& state) {
        VectorClock result(client_id);
        result.clock_ = state;
        if (client_id != 0) result.clock_.try_emplace(client_id, 0);
        return result;
    }

    const std::unordered_map<uint64_t, uint64_t>& getState() const noexcept {
        return clock_;
    }

    uint64_t clientId() const noexcept { return my_id_; }
};

} // namespace omnisync::core
