#ifndef OMNISYNC_HPP
#define OMNISYNC_HPP

/*
 *  ___                _  _____                 
 * / _ \ _ __ ___  _ __(_)___  /_   _ _ __   ___ 
 *| | | | '_ ` _ \| '_ \| / __|| | | | '_ \ / __|
 *| |_| | | | | | | | | | \__ \| |_| | | | | (__ 
 * \___/|_| |_| |_|_| |_|_|___(_)__, |_| |_|\___|
 *                               |___/           
 * 
 * OmniSync: A C++17 Header-Only CRDT Library
 * Copyright (c) 2026 Puneeth R
 * Licensed under the MIT License
 * 
 * Performance:
 * - Delta Sync:     90% bandwidth reduction
 * - VLE Encoding:   82% size reduction  
 * - Combined:       98% total bandwidth reduction (56x smaller!)
 * - Avg atom size:  6 bytes (vs 34 bytes)
 */

// Core Components
#include "core/crdt_atom.hpp"
#include "core/lamport_clock.hpp"
#include "core/vector_clock.hpp"
#include "core/sequence.hpp"

// Network Helpers
#include "network/vle_encoding.hpp"
#include "network/binary_packer.hpp"
#include "network/udp_socket.hpp"

namespace omnisync {
    // Official Release Version
    constexpr int VERSION_MAJOR = 1;
    constexpr int VERSION_MINOR = 3;
    constexpr int VERSION_PATCH = 0;

    static const char* VERSION_STRING = "1.3.0";
    static const char* VERSION_NAME = "Memory Master";
}

#endif // OMNISYNC_HPP
