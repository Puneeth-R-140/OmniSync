#pragma once

#include <vector>
#include <cstdint>
#include <iostream>

namespace omnisync {
namespace network {

/**
 * @brief Variable-Length Encoding using LEB128 (Little Endian Base 128).
 * 
 * This is the same encoding used by:
 * - Protocol Buffers (Google)
 * - DWARF debug format
 * - WebAssembly
 * - Yjs CRDT library
 * 
 * Each byte uses 7 bits for data and 1 bit as continuation flag.
 * 
 * Examples:
 * - 0 → [0x00] (1 byte)
 * - 127 → [0x7F] (1 byte)  
 * - 128 → [0x80, 0x01] (2 bytes)
 * - 16384 → [0x80, 0x80, 0x01] (3 bytes)
 * 
 * Typical CRDT usage:
 * - Client ID (1-100 users): 1-2 bytes
 * - Clock (edits within seconds): 1-3 bytes
 * - Total OpID: 2-6 bytes vs 16 bytes fixed = 62-87% reduction
 */
class VLEEncoding {
public:
    /**
     * @brief Encode unsigned 64-bit integer to variable-length bytes.
     * @param value The number to encode
     * @param out Output buffer (bytes are appended)
     */
    static void encodeUInt64(uint64_t value, std::vector<uint8_t>& out) {
        do {
            uint8_t byte = value & 0x7F; // Lower 7 bits
            value >>= 7;
            
            if (value != 0) {
                byte |= 0x80; // Set continuation bit
            }
            
            out.push_back(byte);
        } while (value != 0);
    }

    /**
     * @brief Decode variable-length bytes to unsigned 64-bit integer.
     * @param in Input buffer
     * @param offset Starting position (will be updated to point after decoded bytes)
     * @param out_value Decoded value
     * @return true if successful, false if buffer too short or invalid encoding
     */
    static bool decodeUInt64(const std::vector<uint8_t>& in, size_t& offset, uint64_t& out_value) {
        out_value = 0;
        int shift = 0;
        
        while (offset < in.size()) {
            uint8_t byte = in[offset++];
            
            // Extract 7 data bits
            out_value |= (uint64_t)(byte & 0x7F) << shift;
            
            // Check continuation bit
            if ((byte & 0x80) == 0) {
                return true; // Done
            }
            
            shift += 7;
            
            // Prevent overflow (64 bits / 7 bits per byte = max 10 bytes)
            if (shift >= 64) {
                return false; // Invalid: too many bytes
            }
        }
        
        return false; // Buffer ended mid-number
    }

    /**
     * @brief Convenience: Encode to new buffer.
     */
    static std::vector<uint8_t> encode(uint64_t value) {
        std::vector<uint8_t> result;
        encodeUInt64(value, result);
        return result;
    }

    /**
     * @brief Convenience: Decode from buffer start.
     */
    static bool decode(const std::vector<uint8_t>& in, uint64_t& out_value) {
        size_t offset = 0;
        return decodeUInt64(in, offset, out_value);
    }

    /**
     * @brief Calculate encoded size without actually encoding.
     * Useful for pre-allocating buffers.
     */
    static size_t encodedSize(uint64_t value) {
        if (value == 0) return 1;
        
        size_t bytes = 0;
        while (value != 0) {
            value >>= 7;
            bytes++;
        }
        return bytes;
    }

    /**
     * @brief Encode signed 64-bit integer using ZigZag encoding.
     * 
     * ZigZag maps signed integers to unsigned:
     * - 0 → 0
     * - -1 → 1
     * - 1 → 2
     * - -2 → 3
     * - 2 → 4
     * 
     * This makes small negative numbers encode efficiently.
     * (Not needed for CRDTs, but included for completeness)
     */
    static void encodeInt64(int64_t value, std::vector<uint8_t>& out) {
        // ZigZag encoding: (n << 1) ^ (n >> 63)
        uint64_t zigzag = (uint64_t)((value << 1) ^ (value >> 63));
        encodeUInt64(zigzag, out);
    }

    /**
     * @brief Decode ZigZag encoded signed integer.
     */
    static bool decodeInt64(const std::vector<uint8_t>& in, size_t& offset, int64_t& out_value) {
        uint64_t zigzag;
        if (!decodeUInt64(in, offset, zigzag)) {
            return false;
        }
        
        // ZigZag decoding: (n >> 1) ^ -(n & 1)
        out_value = (int64_t)((zigzag >> 1) ^ -(int64_t)(zigzag & 1));
        return true;
    }

    /**
     * @brief Stream output operator for easy writing.
     */
    static void writeUInt64(std::ostream& out, uint64_t value) {
        std::vector<uint8_t> bytes;
        encodeUInt64(value, bytes);
        out.write((char*)bytes.data(), bytes.size());
    }

    /**
     * @brief Stream input operator for easy reading.
     */
    static bool readUInt64(std::istream& in, uint64_t& out_value) {
        out_value = 0;
        int shift = 0;
        
        while (true) {
            char byte_char;
            if (!in.read(&byte_char, 1)) {
                return false; // Stream error
            }
            
            uint8_t byte = (uint8_t)byte_char;
            out_value |= (uint64_t)(byte & 0x7F) << shift;
            
            if ((byte & 0x80) == 0) {
                return true; // Done
            }
            
            shift += 7;
            if (shift >= 64) {
                return false; // Overflow
            }
        }
    }
};

} // namespace network
} // namespace omnisync
