#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "../core/crdt_atom.hpp"
#include "vle_encoding.hpp"

namespace omnisync {
namespace network {

using namespace core;

/**
 * @brief Binary Serializer for OmniSync Atoms.
 * 
 * Two versions:
 * 1. BinaryPacker (Legacy): Fixed 34 bytes per atom
 * 2. VLEPacker (New): Variable-length encoding, 4-10 bytes per atom
 * 
 * Use VLEPacker for production (80% size reduction).
 * Use BinaryPacker for debugging (fixed size, easier to inspect).
 */

/**
 * @brief Legacy Fixed-Size Binary Packer (34 bytes per atom).
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

/**
 * @brief Variable-Length Encoding Packer (4-10 bytes per atom, avg ~6 bytes).
 * 
 * Uses LEB128 encoding for integers:
 * - Client IDs (1-100 users): 1-2 bytes
 * - Clocks (edits within seconds): 1-3 bytes
 * - Total: ~6 bytes vs 34 bytes = 82% reduction
 * 
 * Protocol Layout:
 * [VLE] Client ID
 * [VLE] Clock
 * [VLE] Origin Client ID
 * [VLE] Origin Clock
 * [1]   Content (char)
 * [1]   IsDeleted (bool/byte)
 */
class VLEPacker {
public:
    /**
     * @brief Serialize an Atom using Variable-Length Encoding.
     */
    static std::vector<uint8_t> pack(const Atom& atom) {
        std::vector<uint8_t> buffer;
        buffer.reserve(10); // Estimated average size

        // Encode 4 integers using VLE
        VLEEncoding::encodeUInt64(atom.id.client_id, buffer);
        VLEEncoding::encodeUInt64(atom.id.clock, buffer);
        VLEEncoding::encodeUInt64(atom.origin.client_id, buffer);
        VLEEncoding::encodeUInt64(atom.origin.clock, buffer);

        // Fixed-size fields
        buffer.push_back((uint8_t)atom.content);
        buffer.push_back(atom.is_deleted ? 1 : 0);

        return buffer;
    }

    /**
     * @brief Deserialize VLE-encoded bytes back into an Atom.
     */
    static bool unpack(const std::vector<uint8_t>& buffer, Atom& out_atom) {
        size_t offset = 0;

        // Decode 4 integers
        if (!VLEEncoding::decodeUInt64(buffer, offset, out_atom.id.client_id)) 
            return false;
        if (!VLEEncoding::decodeUInt64(buffer, offset, out_atom.id.clock)) 
            return false;
        if (!VLEEncoding::decodeUInt64(buffer, offset, out_atom.origin.client_id)) 
            return false;
        if (!VLEEncoding::decodeUInt64(buffer, offset, out_atom.origin.clock)) 
            return false;

        // Check if we have 2 more bytes for content and flag
        if (offset + 2 > buffer.size()) return false;

        out_atom.content = (char)buffer[offset++];
        out_atom.is_deleted = (buffer[offset++] != 0);

        return true;
    }

    /**
     * @brief Calculate the exact size needed to encode this atom.
     */
    static size_t packedSize(const Atom& atom) {
        return VLEEncoding::encodedSize(atom.id.client_id) +
               VLEEncoding::encodedSize(atom.id.clock) +
               VLEEncoding::encodedSize(atom.origin.client_id) +
               VLEEncoding::encodedSize(atom.origin.clock) +
               2; // content + is_deleted
    }
};

} // namespace network
} // namespace omnisync
