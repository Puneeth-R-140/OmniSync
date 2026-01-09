#pragma once

#include <cstdint>
#include <string>
#include <functional> // for std::hash

namespace omnisync {
namespace core {

/**
 * @brief Unique Identifier for any Operation in the system.
 * Consists of (Who, When).
 */
struct OpID {
    uint64_t client_id; // Who wrote this? (Unique per device)
    uint64_t clock;     // When? (Logical Lamport Timestamp)

    // Standard Equality Operator
    bool operator==(const OpID& other) const {
        return clock == other.clock && client_id == other.client_id;
    }

    bool operator!=(const OpID& other) const {
        return !(*this == other);
    }

    /**
     * @brief Sorting Rule (CRUCIAL for CRDTs)
     * Defines the "Total Ordering" of events.
     * 1. Check Clock (Older events first, newer events last).
     * 2. If Clocks match, check ClientID (Arbitrary tie-breaker).
     */
    bool operator<(const OpID& other) const {
        if (clock != other.clock) {
            return clock < other.clock;
        }
        return client_id < other.client_id;
    }
};

/**
 * @brief The fundamental unit of the data structure (RGA Node).
 * Represents a single character insertion.
 */
struct Atom {
    OpID id;          // My unique ID
    OpID origin;      // The ID of the Atom strictly to my LEFT (Parent)
    
    char content;     // The payload (e.g., 'A')
    bool is_deleted;  // If true, this is a "Tombstone" (Invisible, but kept for history)

    // Constructor for convenience
    Atom(OpID _id, OpID _origin, char _content)
        : id(_id), origin(_origin), content(_content), is_deleted(false) {}
        
    // Default constructor needed for some containers
    Atom() : id({0,0}), origin({0,0}), content(0), is_deleted(true) {}
};

} // namespace core
} // namespace omnisync

// Inject Hash Function into std namespace so we can use OpID as a Map Key
namespace std {
    template<>
    struct hash<omnisync::core::OpID> {
        size_t operator()(const omnisync::core::OpID& k) const {
            // Simple hash combination
            return std::hash<uint64_t>()(k.client_id) ^ (std::hash<uint64_t>()(k.clock) << 1);
        }
    };
}
