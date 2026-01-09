#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "../core/crdt_atom.hpp"

namespace omnisync {
namespace network {

using namespace core;

/**
 * @brief Simple Binary Serializer for OmniSync Atoms.
 * Converts Atoms to/from raw bytes for network transmission.
 * Endianness: Little-Endian (Standard for x86/ARM).
 */
class BinaryPacker {
public:
    /**
     * @brief Serialize an Atom into a byte buffer.
     * Protocol Layout:
     * [0-7]   Client ID
     * [8-15]  Clock
     * [16-23] Origin Client ID
     * [24-31] Origin Clock
     * [32]    Content (char)
     * [33]    IsDeleted (bool/byte)
     */
    static std::vector<uint8_t> pack(const Atom& atom) {
        std::vector<uint8_t> buffer;
        buffer.reserve(34);

        // Helper Lambda to push 64-bit integers
        auto push_u64 = [&](uint64_t val) {
            for(int i=0; i<8; i++) {
                buffer.push_back((val >> (i * 8)) & 0xFF);
            }
        };

        push_u64(atom.id.client_id);
        push_u64(atom.id.clock);
        push_u64(atom.origin.client_id);
        push_u64(atom.origin.clock);

        buffer.push_back((uint8_t)atom.content);
        buffer.push_back(atom.is_deleted ? 1 : 0);

        return buffer;
    }

    /**
     * @brief Unserialize bytes back into an Atom.
     * @return true if successful, false if buffer is too small.
     */
    static bool unpack(const std::vector<uint8_t>& buffer, Atom& out_atom) {
        if (buffer.size() < 34) return false;

        auto read_u64 = [&](size_t offset) -> uint64_t {
            uint64_t val = 0;
            for(int i=0; i<8; i++) {
                val |= ((uint64_t)buffer[offset + i] << (i * 8));
            }
            return val;
        };

        out_atom.id.client_id = read_u64(0);
        out_atom.id.clock     = read_u64(8);
        out_atom.origin.client_id = read_u64(16);
        out_atom.origin.clock     = read_u64(24);
        
        out_atom.content = (char)buffer[32];
        out_atom.is_deleted = (buffer[33] != 0);

        return true;
    }
};

} // namespace network
} // namespace omnisync
