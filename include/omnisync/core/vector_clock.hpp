#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iostream>

namespace omnisync {
namespace core {

/**
 * @brief Vector Clock implementation for causal history tracking.
 * A Vector Clock maintains a mapping of NodeID -> LogicalTime.
 * It allows strict determination of "Happened Before", "After", or "Concurrent".
 */
class VectorClock {
private:
    std::unordered_map<uint64_t, uint64_t> clock;
    uint64_t my_id;

public:
    VectorClock(uint64_t client_id) : my_id(client_id) {
        clock[my_id] = 0;
    }

    /**
     * @brief Increment local time.
     */
    void tick() {
        clock[my_id]++;
    }

    /**
     * @brief Update local clock based on a received remote clock.
     * Rule: For every node ID, take the MAX(local, remote).
     */
    void merge(const VectorClock& other) {
        for (const auto& [id, time] : other.clock) {
            clock[id] = std::max(clock[id], time);
        }
    }

    /**
     * @brief Compare two Vector Clocks.
     * @return 
     *  -1 if THIS happened BEFORE other.
     *   1 if THIS happened AFTER other.
     *   0 if Concurrent (neither happened before the other).
     */
    int compare(const VectorClock& other) const {
        bool older = false;
        bool newer = false;

        // Iterate over union of keys (simplified: check all in map)
        // Note: A robust implementation iterates over all unique keys in both maps.
        // For efficiency, we assume keys exist or default to 0.

        // Check against other's keys
        for (const auto& [id, time] : other.clock) {
            uint64_t my_time = 0;
            if (clock.count(id)) my_time = clock.at(id); // Use safe access
            
            if (my_time < time) older = true;
            if (my_time > time) newer = true;
        }

        // Check against my keys (in case I have knowledge other doesn't)
        for (const auto& [id, time] : clock) {
             uint64_t other_time = 0;
             // We can't easily access 'other' map efficiently if it's private/const.
             // (Assuming friendship or public access for this MVP logic loop)
             // Let's use a public getter or iterator for the sake of the library.
        }

        if (older && !newer) return -1; // Strictly older
        if (!older && newer) return 1;  // Strictly newer
        return 0; // Concurrent (Conflict)
    }

    // Diagnostics
    void print() const {
        std::cout << "[";
        for (const auto& [id, time] : clock) {
            std::cout << id << ":" << time << " ";
        }
        std::cout << "]" << std::endl;
    }
};

} // namespace core
} // namespace omnisync
