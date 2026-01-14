#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstdint>

namespace omnisync {
namespace core {

/**
 * @brief Production-ready Vector Clock for causal history tracking.
 * 
 * A Vector Clock maintains a mapping of ClientID -> LogicalTime.
 * It enables strict determination of causal relationships:
 * - "Happened Before" (A < B)
 * - "Happened After" (A > B)  
 * - "Concurrent" (A || B)
 * 
 * Used for:
 * 1. Delta Sync: Determine which operations a peer is missing
 * 2. Garbage Collection: Find the stable frontier for safe pruning
 * 3. Conflict Detection: Identify concurrent edits
 */
class VectorClock {
private:
    std::unordered_map<uint64_t, uint64_t> clock;
    uint64_t my_id;

public:
    VectorClock() : my_id(0) {}
    
    VectorClock(uint64_t client_id) : my_id(client_id) {
        clock[my_id] = 0;
    }

    /**
     * @brief Get current time for a specific client.
     */
    uint64_t get(uint64_t client_id) const {
        auto it = clock.find(client_id);
        return (it != clock.end()) ? it->second : 0;
    }

    /**
     * @brief Increment local time.
     */
    void tick() {
        clock[my_id]++;
    }

    /**
     * @brief Update with a specific client's timestamp.
     */
    void update(uint64_t client_id, uint64_t time) {
        clock[client_id] = std::max(clock[client_id], time);
    }

    /**
     * @brief Merge with another vector clock (take max of each entry).
     * This is the standard vector clock merge operation.
     */
    void merge(const VectorClock& other) {
        for (const auto& [id, time] : other.clock) {
            clock[id] = std::max(clock[id], time);
        }
    }

    /**
     * @brief Compare two Vector Clocks.
     * @return 
     *  -1 if THIS happened BEFORE other (this < other)
     *   1 if THIS happened AFTER other (this > other)
     *   0 if Concurrent (neither happened before the other)
     */
    int compare(const VectorClock& other) const {
        bool this_less = false;
        bool this_greater = false;

        // Collect all unique client IDs from both clocks
        std::unordered_map<uint64_t, bool> all_ids;
        for (const auto& [id, _] : clock) all_ids[id] = true;
        for (const auto& [id, _] : other.clock) all_ids[id] = true;

        // Compare each client's time
        for (const auto& [id, _] : all_ids) {
            uint64_t my_time = get(id);
            uint64_t other_time = other.get(id);
            
            if (my_time < other_time) this_less = true;
            if (my_time > other_time) this_greater = true;
        }

        if (this_less && !this_greater) return -1;  // Strictly before
        if (!this_less && this_greater) return 1;   // Strictly after
        if (!this_less && !this_greater) return 0;  // Equal
        return 0; // Concurrent
    }

    /**
     * @brief Check if this clock is strictly less than other.
     * (All entries <= other, at least one <)
     */
    bool operator<(const VectorClock& other) const {
        return compare(other) == -1;
    }

    /**
     * @brief Check if this clock is concurrent with other.
     */
    bool isConcurrent(const VectorClock& other) const {
        int cmp = compare(other);
        // Concurrent means neither < nor > nor ==
        bool this_less = false, this_greater = false;
        
        for (const auto& [id, time] : clock) {
            uint64_t other_time = other.get(id);
            if (time < other_time) this_less = true;
            if (time > other_time) this_greater = true;
        }
        for (const auto& [id, time] : other.clock) {
            if (clock.find(id) == clock.end()) {
                if (time > 0) this_greater = true;
            }
        }
        
        return this_less && this_greater;
    }

    /**
     * @brief Get the minimum time across all clients.
     * Used for garbage collection: any operation older than this is stable.
     */
    uint64_t getMinTime() const {
        if (clock.empty()) return 0;
        
        uint64_t min_time = UINT64_MAX;
        for (const auto& [id, time] : clock) {
            min_time = std::min(min_time, time);
        }
        return min_time;
    }

    /**
     * @brief Compute the minimum vector clock from multiple clocks.
     * This represents the "stable frontier" - what all peers have seen.
     */
    static VectorClock computeMinimum(const std::vector<VectorClock>& clocks) {
        if (clocks.empty()) return VectorClock();
        
        VectorClock result = clocks[0];
        
        // Collect all client IDs
        std::unordered_map<uint64_t, uint64_t> min_times;
        for (const auto& vc : clocks) {
            for (const auto& [id, time] : vc.clock) {
                if (min_times.find(id) == min_times.end()) {
                    min_times[id] = UINT64_MAX;
                }
            }
        }
        
        // Find minimum for each client
        for (auto& [id, min_time] : min_times) {
            for (const auto& vc : clocks) {
                min_time = std::min(min_time, vc.get(id));
            }
        }
        
        result.clock = min_times;
        return result;
    }

    /**
     * @brief Serialize to binary stream.
     */
    void save(std::ostream& out) const {
        uint32_t count = clock.size();
        out.write((char*)&count, sizeof(count));
        
        for (const auto& [id, time] : clock) {
            out.write((char*)&id, sizeof(id));
            out.write((char*)&time, sizeof(time));
        }
    }

    /**
     * @brief Deserialize from binary stream.
     */
    bool load(std::istream& in) {
        clock.clear();
        
        uint32_t count;
        in.read((char*)&count, sizeof(count));
        
        for (uint32_t i = 0; i < count; i++) {
            uint64_t id, time;
            in.read((char*)&id, sizeof(id));
            in.read((char*)&time, sizeof(time));
            clock[id] = time;
        }
        
        return true;
    }

    /**
     * @brief Debug output.
     */
    void print() const {
        std::cout << "[";
        bool first = true;
        for (const auto& [id, time] : clock) {
            if (!first) std::cout << ", ";
            std::cout << id << ":" << time;
            first = false;
        }
        std::cout << "]";
    }

    // Expose internal state for delta computation
    const std::unordered_map<uint64_t, uint64_t>& getState() const {
        return clock;
    }
};

} // namespace core
} // namespace omnisync
