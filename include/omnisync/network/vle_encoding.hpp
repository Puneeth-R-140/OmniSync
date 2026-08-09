#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>

namespace omnisync::network {

/**
 * @brief Canonical unsigned LEB128 codec used by OmniSync's wire format.
 *
 * The decoder is deliberately strict:
 * - at most 10 bytes are accepted for uint64_t;
 * - the tenth byte may contain only bit 0 as payload;
 * - malformed/truncated encodings are rejected;
 * - non-canonical encodings are rejected.
 *
 * This matters because network input is untrusted. A decoder must never rely
 * on unsigned-shift wraparound to reject an oversized integer.
 */
class VLEEncoding {
public:
    static constexpr std::size_t kMaxUInt64Bytes = 10;

    static void encodeUInt64(uint64_t value, std::vector<uint8_t>& out) {
        do {
            uint8_t byte = static_cast<uint8_t>(value & 0x7Fu);
            value >>= 7;
            if (value != 0) {
                byte |= 0x80u;
            }
            out.push_back(byte);
        } while (value != 0);
    }

    static bool decodeUInt64(
        const std::vector<uint8_t>& in,
        std::size_t& offset,
        uint64_t& out_value) noexcept
    {
        if (offset >= in.size()) {
            return false;
        }

        const std::size_t start = offset;
        uint64_t value = 0;

        for (std::size_t i = 0; i < kMaxUInt64Bytes; ++i) {
            if (offset >= in.size()) {
                return false;
            }

            const uint8_t byte = in[offset++];
            const uint8_t payload = static_cast<uint8_t>(byte & 0x7Fu);

            // A uint64_t has only one usable payload bit in byte 10.
            if (i == 9 && payload > 1u) {
                return false;
            }

            value |= static_cast<uint64_t>(payload) << (7u * i);

            if ((byte & 0x80u) == 0) {
                // Canonical LEB128: values that fit in fewer bytes must not
                // use a redundant zero-valued continuation byte.
                const std::size_t encoded_bytes = offset - start;
                if (encoded_bytes > 1 && payload == 0) {
                    // A terminating zero payload is redundant for any
                    // multi-byte encoding: the preceding byte could have
                    // terminated the value instead.
                    return false;
                }

                out_value = value;
                return true;
            }
        }

        // Ten bytes were consumed and byte 10 still requested continuation.
        return false;
    }

    static std::vector<uint8_t> encode(uint64_t value) {
        std::vector<uint8_t> result;
        result.reserve(encodedSize(value));
        encodeUInt64(value, result);
        return result;
    }

    static bool decode(
        const std::vector<uint8_t>& in,
        uint64_t& out_value) noexcept
    {
        std::size_t offset = 0;
        return decodeUInt64(in, offset, out_value) && offset == in.size();
    }

    static std::size_t encodedSize(uint64_t value) noexcept {
        std::size_t size = 1;
        while (value >= 128) {
            value >>= 7;
            ++size;
        }
        return size;
    }

    static void encodeInt64(int64_t value, std::vector<uint8_t>& out) {
        // ZigZag using only unsigned arithmetic. This avoids both signed
        // left-shift overflow and implementation-defined right-shift behavior
        // for negative int64_t values.
        const uint64_t bits = static_cast<uint64_t>(value);
        const uint64_t zigzag =
            (bits << 1) ^ (uint64_t{0} - (bits >> 63));
        encodeUInt64(zigzag, out);
    }

    static bool decodeInt64(
        const std::vector<uint8_t>& in,
        std::size_t& offset,
        int64_t& out_value) noexcept
    {
        uint64_t zigzag = 0;
        if (!decodeUInt64(in, offset, zigzag)) {
            return false;
        }

        const uint64_t sign_mask = uint64_t{0} - (zigzag & 1u);
        const uint64_t bits = (zigzag >> 1) ^ sign_mask;
        out_value = static_cast<int64_t>(bits);
        return true;
    }

    static bool writeUInt64(std::ostream& out, uint64_t value) {
        uint8_t bytes[kMaxUInt64Bytes];
        std::size_t count = 0;

        do {
            uint8_t byte = static_cast<uint8_t>(value & 0x7Fu);
            value >>= 7;
            if (value != 0) {
                byte |= 0x80u;
            }
            bytes[count++] = byte;
        } while (value != 0);

        out.write(reinterpret_cast<const char*>(bytes),
                  static_cast<std::streamsize>(count));
        return static_cast<bool>(out);
    }

    static bool readUInt64(std::istream& in, uint64_t& out_value) {
        uint64_t value = 0;

        for (std::size_t i = 0; i < kMaxUInt64Bytes; ++i) {
            char raw = 0;
            if (!in.get(raw)) {
                return false;
            }

            const uint8_t byte = static_cast<uint8_t>(
                static_cast<unsigned char>(raw));
            const uint8_t payload = static_cast<uint8_t>(byte & 0x7Fu);

            // A uint64_t has only one usable payload bit in byte 10.
            if (i == 9 && payload > 1u) {
                return false;
            }

            value |= static_cast<uint64_t>(payload) << (7u * i);

            if ((byte & 0x80u) == 0) {
                // Match decodeUInt64(): reject non-canonical encodings such
                // as {0x81, 0x00}, which represent 1 using two bytes.
                if (i > 0 && payload == 0) {
                    return false;
                }

                out_value = value;
                return true;
            }
        }

        return false;
    }
};

} // namespace omnisync::network
