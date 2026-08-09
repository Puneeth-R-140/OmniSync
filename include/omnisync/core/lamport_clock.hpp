#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>

namespace omnisync::core {

/**
 * @brief Hybrid Logical Clock exposed through the historical LamportClock API.
 *
 * Correctness never depends on physical time. The returned value is a packed
 * HLC timestamp: 48 bits of physical milliseconds and 16 bits of logical
 * counter. A configurable forward-skew cap prevents a badly configured host
 * clock from permanently pushing the logical timeline arbitrarily far into the
 * future. Backward clock changes are handled automatically.
 *
 * The clock is monotonic and safe for concurrent tick()/merge()/peek() calls.
 * Configuration access is atomic, so setConfig()/getConfig() may safely run
 * concurrently with tick()/merge().
 */
class LamportClock {
public:
    struct Config {
        // Maximum amount by which a local wall clock may move the HLC forward
        // relative to the last observed physical component.
        uint64_t max_forward_skew_ms = 24ULL * 60ULL * 60ULL * 1000ULL;
    };

private:
    static constexpr unsigned kLogicalBits = 16;
    static constexpr uint64_t kLogicalMask = (1ULL << kLogicalBits) - 1ULL;
    static constexpr uint64_t kPhysicalMask = (1ULL << 48) - 1ULL;

    std::atomic<uint64_t> value_{0};
    std::atomic<uint64_t> max_forward_skew_ms_{24ULL * 60ULL * 60ULL * 1000ULL};

    static uint64_t physicalMillis() noexcept {
        const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
        const auto count = now.time_since_epoch().count();
        return count < 0 ? 0ULL : static_cast<uint64_t>(count);
    }

    static uint64_t physicalPart(uint64_t packed) noexcept {
        return (packed >> kLogicalBits) & kPhysicalMask;
    }

    static uint64_t logicalPart(uint64_t packed) noexcept {
        return packed & kLogicalMask;
    }

    static uint64_t pack(uint64_t physical, uint64_t logical) noexcept {
        physical = std::min(physical, kPhysicalMask);
        logical = std::min(logical, kLogicalMask);
        return (physical << kLogicalBits) | logical;
    }

    static uint64_t saturatingAdd(uint64_t a, uint64_t b) noexcept {
        if (b > std::numeric_limits<uint64_t>::max() - a) {
            return std::numeric_limits<uint64_t>::max();
        }
        return a + b;
    }

    uint64_t boundedPhysical(uint64_t observed, uint64_t previous) const noexcept {
        const uint64_t previous_physical = physicalPart(previous);
        if (observed <= previous_physical) return observed;

        const uint64_t cap = max_forward_skew_ms_.load(std::memory_order_acquire);
        if (cap == std::numeric_limits<uint64_t>::max()) {
            return std::min(observed, kPhysicalMask);
        }

        const uint64_t bounded = saturatingAdd(previous_physical, cap);
        return std::min(observed, std::min(bounded, kPhysicalMask));
    }

    /**
     * @brief Increment a logical component without allowing wraparound.
     *
     * Returns false when the logical field is saturated. The caller must then
     * advance the physical component or return a saturated timestamp.
     */
    static bool incrementLogical(uint64_t logical, uint64_t& result) noexcept {
        if (logical >= kLogicalMask) return false;
        result = logical + 1;
        return true;
    }

public:
    LamportClock() = default;

    explicit LamportClock(const Config& config)
        : max_forward_skew_ms_(config.max_forward_skew_ms) {}

    LamportClock(const LamportClock& other)
        : value_(other.peek()),
          max_forward_skew_ms_(other.max_forward_skew_ms_.load(std::memory_order_acquire)) {}

    LamportClock& operator=(const LamportClock& other) {
        if (this != &other) {
            max_forward_skew_ms_.store(
                other.max_forward_skew_ms_.load(std::memory_order_acquire),
                std::memory_order_release);
            value_.store(other.peek(), std::memory_order_release);
        }
        return *this;
    }

    LamportClock(LamportClock&& other) noexcept
        : value_(other.peek()),
          max_forward_skew_ms_(other.max_forward_skew_ms_.load(std::memory_order_acquire)) {}

    LamportClock& operator=(LamportClock&& other) noexcept {
        if (this != &other) {
            max_forward_skew_ms_.store(
                other.max_forward_skew_ms_.load(std::memory_order_acquire),
                std::memory_order_release);
            value_.store(other.peek(), std::memory_order_release);
        }
        return *this;
    }

    uint64_t peek() const noexcept {
        return value_.load(std::memory_order_acquire);
    }

    /** Create a strictly newer local HLC timestamp. */
    uint64_t tick() noexcept {
        uint64_t current = value_.load(std::memory_order_relaxed);

        for (;;) {
            const uint64_t observed_physical = physicalMillis();
            const uint64_t current_physical = physicalPart(current);
            const uint64_t current_logical = logicalPart(current);

            uint64_t physical = boundedPhysical(observed_physical, current);
            physical = std::max(physical, current_physical);

            uint64_t logical = 0;
            if (physical == current_physical) {
                if (!incrementLogical(current_logical, logical)) {
                    // Logical overflow: advance the physical component by one
                    // millisecond rather than wrapping and breaking monotonicity.
                    if (current_physical == kPhysicalMask) {
                        // The complete packed timestamp is saturated. There is
                        // no representable strictly newer value.
                        return current;
                    }
                    physical = current_physical + 1;
                    logical = 0;
                }
            }

            const uint64_t next = pack(physical, logical);
            if (next <= current) {
                // This can only happen at the representable upper boundary.
                return current;
            }

            if (value_.compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return next;
            }
        }
    }

    /**
     * @brief Merge a received timestamp according to HLC receive semantics.
     *
     * This operation records the causal observation of the remote timestamp;
     * it does not create an application operation. The next tick() is strictly
     * greater than the resulting state unless the representable timestamp
     * space is saturated.
     */
    void merge(uint64_t received) noexcept {
        uint64_t current = value_.load(std::memory_order_relaxed);

        for (;;) {
            const uint64_t now = physicalMillis();
            const uint64_t local_physical = physicalPart(current);
            const uint64_t local_logical = logicalPart(current);
            const uint64_t remote_physical = physicalPart(received);
            const uint64_t remote_logical = logicalPart(received);

            // A remote timestamp can legitimately be ahead of our local wall
            // clock. Do not apply the local forward-skew cap to received causal
            // information; doing so could make a causally newer timestamp look
            // older and break the HLC ordering invariant.
            const uint64_t physical = std::max({
                std::min(now, kPhysicalMask),
                local_physical,
                remote_physical
            });

            uint64_t logical = 0;

            if (physical == local_physical && physical == remote_physical) {
                const uint64_t base = std::max(local_logical, remote_logical);
                if (!incrementLogical(base, logical)) {
                    if (physical == kPhysicalMask) {
                        return; // Timestamp space is saturated.
                    }
                    const uint64_t next = pack(physical + 1, 0);
                    if (value_.compare_exchange_weak(
                            current,
                            next,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed)) {
                        return;
                    }
                    continue;
                }
            } else if (physical == local_physical) {
                if (!incrementLogical(local_logical, logical)) {
                    if (physical == kPhysicalMask) return;
                    const uint64_t next = pack(physical + 1, 0);
                    if (value_.compare_exchange_weak(
                            current,
                            next,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed)) {
                        return;
                    }
                    continue;
                }
            } else if (physical == remote_physical) {
                if (!incrementLogical(remote_logical, logical)) {
                    if (physical == kPhysicalMask) return;
                    const uint64_t next = pack(physical + 1, 0);
                    if (value_.compare_exchange_weak(
                            current,
                            next,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed)) {
                        return;
                    }
                    continue;
                }
            }

            const uint64_t next = pack(physical, logical);
            if (next <= current) {
                return;
            }

            if (value_.compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return;
            }
        }
    }

    Config getConfig() const noexcept {
        Config config;
        config.max_forward_skew_ms =
            max_forward_skew_ms_.load(std::memory_order_acquire);
        return config;
    }

    void setConfig(const Config& config) noexcept {
        max_forward_skew_ms_.store(
            config.max_forward_skew_ms,
            std::memory_order_release);
    }
};

} // namespace omnisync::core
