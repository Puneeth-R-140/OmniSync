#pragma once

/**
 * OmniSync: A C++17 Header-Only CRDT Library
 * Copyright (c) 2026 Puneeth R
 * Licensed under the MIT License
 *
 * Public umbrella header for the CRDT core and networking interfaces.
 * Performance characteristics are workload-dependent and are established by
 * the benchmark suite rather than treated as API guarantees.
 */

#include "core/crdt_atom.hpp"
#include "core/lamport_clock.hpp"
#include "core/vector_clock.hpp"
#include "core/memory_stats.hpp"
#include "core/sequence.hpp"
#include "core/gc_coordinator.hpp"

#include "network/vle_encoding.hpp"
#include "network/binary_packer.hpp"
#include "network/udp_socket.hpp"

namespace omnisync {

inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 0;
inline constexpr int VERSION_PATCH = 0;
inline constexpr const char VERSION_STRING[] = "0.0.5-dev";
inline constexpr const char VERSION_NAME[] = "Development";

} // namespace omnisync
