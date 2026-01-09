#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace omnisync {
namespace core {

/**
 * @brief Logical Clock for Distributed Systems (Lamport Timestamp)
 * 
 * Unlike a physical clock (Wall Time), a Logical Clock only tracks "Order of Events".
 * It ensures that if Event A caused Event B, then Clock(A) < Clock(B).
 * 
 * Rules:
 * 1. Local Event: clock = clock + 1
 * 2. Send Message: attach current clock
 * 3. Receive Message: clock = max(local_clock, message_clock) + 1
 */
class LamportClock {
private:
    // Atomic ensures thread-safety if multiple threads try to "tick" at once.
    std::atomic<uint64_t> counter;

public:
    // Start at Time 0
    LamportClock() : counter(0) {}

    /**
     * @brief Get the current logical time without changing it.
     */
    uint64_t peek() const {
        return counter.load();
    }

    /**
     * @brief Advance the clock for a local operation (e.g., user types a key).
     * @return The new timestamp for the operation.
     */
    uint64_t tick() {
        return ++counter;
    }

    /**
     * @brief Synchronization Step: Update our clock based on a received message.
     * We must jump ahead if the other person is in the "future".
     * 
     * @param received_time The timestamp attached to the incoming message.
     */
    void merge(uint64_t received_time) {
        uint64_t current = counter.load();
        while (true) {
            // formula: time = max(ours, theirs) + 1
            uint64_t next = std::max(current, received_time) + 1;
            
            // Compare-And-Swap (CAS) Loop for thread safety
            // If counter == current, set it to next. Otherwise, reload current and try again.
            if (counter.compare_exchange_weak(current, next)) {
                break;
            }
        }
    }
};

} // namespace core
} // namespace omnisync
