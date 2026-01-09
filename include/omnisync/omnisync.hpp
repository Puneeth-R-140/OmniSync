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
 * Copyright (c) 2026
 * Licensed under the MIT License
 */

// Core Components
#include "core/crdt_atom.hpp"
#include "core/lamport_clock.hpp"
#include "core/vector_clock.hpp"
#include "core/sequence.hpp"

// Network Helpers
#include "network/binary_packer.hpp"
#include "network/udp_socket.hpp"

namespace omnisync {
    // Official Release Version
    constexpr int VERSION_MAJOR = 1;
    constexpr int VERSION_MINOR = 0;
    constexpr int VERSION_PATCH = 0;

    static const char* VERSION_STRING = "1.0.0";
}

#endif // OMNISYNC_HPP
