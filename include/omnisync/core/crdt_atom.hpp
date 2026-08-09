#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace omnisync::core {

/**
 * @brief Globally unique operation identifier.
 *
 * clock is an HLC timestamp used for deterministic ordering.
 * sequence is a per-client contiguous operation number used by the
 * vector-clock/delta layer. Keeping these two concepts separate avoids
 * using wall-clock time as a causal sequence number.
 */
struct OpID {
    uint64_t client_id = 0;
    uint64_t clock = 0;
    uint64_t sequence = 0;

    constexpr bool isNull() const noexcept {
        return client_id == 0 && clock == 0 && sequence == 0;
    }

    friend constexpr bool operator==(const OpID& a, const OpID& b) noexcept {
        return a.client_id == b.client_id &&
               a.clock == b.clock &&
               a.sequence == b.sequence;
    }

    friend constexpr bool operator!=(const OpID& a, const OpID& b) noexcept {
        return !(a == b);
    }

    /** Deterministic total order: HLC, client ID, then sequence. */
    friend constexpr bool operator<(const OpID& a, const OpID& b) noexcept {
        if (a.clock != b.clock) return a.clock < b.clock;
        if (a.client_id != b.client_id) return a.client_id < b.client_id;
        return a.sequence < b.sequence;
    }
};

/**
 * @brief A single RGA sequence atom.
 *
 * The atom remains in the sequence after deletion so that its identity and
 * ancestry can continue to resolve until distributed GC proves it is safe to
 * remove. delete_operation_ids contains the idempotent delete operations that
 * have been observed for this atom.
 */
struct Atom {
    OpID id;
    OpID origin;                    // Stable RGA parent/left anchor.
    char content = '\0';
    bool is_deleted = false;
    std::vector<OpID> delete_operation_ids;

    Atom() = default;

    Atom(OpID id_, OpID origin_, char content_)
        : id(id_), origin(origin_), content(content_) {}
};

} // namespace omnisync::core

namespace std {
template <>
struct hash<omnisync::core::OpID> {
    std::size_t operator()(const omnisync::core::OpID& value) const noexcept {
        // SplitMix-style combination; unlike XOR this does not lose as much
        // information when the fields have similar bit patterns.
        auto mix = [](std::uint64_t x) noexcept {
            x += 0x9e3779b97f4a7c15ULL;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            return x ^ (x >> 31);
        };

        std::uint64_t h = mix(value.client_id);
        h ^= mix(value.clock + 0x517cc1b727220a95ULL + (h << 6) + (h >> 2));
        h ^= mix(value.sequence + 0x6eed0e9da4d94a4fULL + (h << 6) + (h >> 2));
        return static_cast<std::size_t>(h);
    }
};
} // namespace std
