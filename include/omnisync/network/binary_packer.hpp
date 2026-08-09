#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "../core/crdt_atom.hpp"
#include "vle_encoding.hpp"

namespace omnisync::network {

using core::Atom;
using core::OpID;

/**
 * @brief Versioned wire serializer for OmniSync atoms.
 *
 * The previous packer serialized only {client_id, clock} and silently lost
 * the new OpID sequence field and delete-operation metadata. That is no
 * longer safe after the CRDT identity model was separated into:
 *
 *     { client_id, HLC clock, per-client sequence }
 *
 * The current wire format is therefore versioned and carries the complete
 * atom state.
 *
 * Layout:
 *   magic        : 2 bytes  ('O','S')
 *   version      : 1 byte
 *   id           : 3 x VLE uint64
 *   origin       : 3 x VLE uint64
 *   content      : 1 byte
 *   deleted      : 1 byte
 *   delete_count : VLE uint64
 *   delete IDs   : delete_count x 3 VLE uint64
 *
 * The parser rejects malformed input, excessive collection sizes, invalid
 * boolean values, and trailing bytes.
 */
class BinaryPacker {
public:
    static constexpr uint8_t kVersion = 2;
    static constexpr std::size_t kHeaderSize = 3;
    static constexpr std::size_t kMaxDeleteOperations = 1'000'000;

    static std::vector<uint8_t> pack(const Atom& atom) {
        std::vector<uint8_t> buffer;
        buffer.reserve(packedSize(atom));

        buffer.push_back(static_cast<uint8_t>('O'));
        buffer.push_back(static_cast<uint8_t>('S'));
        buffer.push_back(kVersion);

        appendOpID(buffer, atom.id);
        appendOpID(buffer, atom.origin);
        buffer.push_back(static_cast<uint8_t>(atom.content));
        buffer.push_back(atom.is_deleted ? 1u : 0u);

        VLEEncoding::encodeUInt64(
            static_cast<uint64_t>(atom.delete_operation_ids.size()), buffer);

        for (const OpID& deletion : atom.delete_operation_ids) {
            appendOpID(buffer, deletion);
        }

        return buffer;
    }

    static bool unpack(
        const std::vector<uint8_t>& buffer,
        Atom& out_atom)
    {
        if (buffer.size() < kHeaderSize) return false;

        std::size_t offset = 0;
        if (buffer[offset++] != static_cast<uint8_t>('O') ||
            buffer[offset++] != static_cast<uint8_t>('S') ||
            buffer[offset++] != kVersion) {
            return false;
        }

        Atom decoded;
        if (!readOpID(buffer, offset, decoded.id) ||
            !readOpID(buffer, offset, decoded.origin)) {
            return false;
        }

        if (buffer.size() - offset < 2) return false;

        decoded.content = static_cast<char>(buffer[offset++]);

        const uint8_t deleted = buffer[offset++];
        if (deleted > 1u) return false;
        decoded.is_deleted = deleted != 0;

        uint64_t delete_count = 0;
        if (!VLEEncoding::decodeUInt64(buffer, offset, delete_count) ||
            delete_count > kMaxDeleteOperations) {
            return false;
        }

        // Every OpID contains three VLE values, each requiring at least one
        // byte. Reject impossible counts before allocating or decoding them.
        const uint64_t remaining = static_cast<uint64_t>(buffer.size() - offset);
        if (delete_count > remaining / 3u) return false;

        decoded.delete_operation_ids.reserve(
            static_cast<std::size_t>(delete_count));

        for (uint64_t i = 0; i < delete_count; ++i) {
            OpID deletion;
            if (!readOpID(buffer, offset, deletion)) return false;
            decoded.delete_operation_ids.push_back(deletion);
        }

        if (offset != buffer.size()) return false;

        // A non-deleted atom carrying delete operations is still accepted:
        // the CRDT merge layer decides the resulting state. The serializer
        // should not invent application semantics.
        out_atom = std::move(decoded);
        return true;
    }

    static std::size_t packedSize(const Atom& atom) noexcept {
        std::size_t size = kHeaderSize;
        size = saturatingAdd(size, opIDSize(atom.id));
        size = saturatingAdd(size, opIDSize(atom.origin));
        size = saturatingAdd(size, 2);
        size = saturatingAdd(
            size,
            VLEEncoding::encodedSize(
                static_cast<uint64_t>(atom.delete_operation_ids.size())));

        for (const OpID& deletion : atom.delete_operation_ids) {
            size = saturatingAdd(size, opIDSize(deletion));
        }
        return size;
    }

private:
    static std::size_t saturatingAdd(std::size_t a, std::size_t b) noexcept {
        if (std::numeric_limits<std::size_t>::max() - a < b) {
            return std::numeric_limits<std::size_t>::max();
        }
        return a + b;
    }

    static void appendOpID(std::vector<uint8_t>& out, const OpID& id) {
        VLEEncoding::encodeUInt64(id.client_id, out);
        VLEEncoding::encodeUInt64(id.clock, out);
        VLEEncoding::encodeUInt64(id.sequence, out);
    }

    static bool readOpID(
        const std::vector<uint8_t>& in,
        std::size_t& offset,
        OpID& id)
    {
        return VLEEncoding::decodeUInt64(in, offset, id.client_id) &&
               VLEEncoding::decodeUInt64(in, offset, id.clock) &&
               VLEEncoding::decodeUInt64(in, offset, id.sequence);
    }

    static std::size_t opIDSize(const OpID& id) noexcept {
        return VLEEncoding::encodedSize(id.client_id) +
               VLEEncoding::encodedSize(id.clock) +
               VLEEncoding::encodedSize(id.sequence);
    }
};

/**
 * @brief Compatibility name for the variable-length atom packer.
 *
 * New code should use BinaryPacker directly. Keeping VLEPacker avoids
 * needlessly breaking existing callers.
 */
class VLEPacker {
public:
    static std::vector<uint8_t> pack(const Atom& atom) {
        return BinaryPacker::pack(atom);
    }

    static bool unpack(
        const std::vector<uint8_t>& buffer,
        Atom& out_atom)
    {
        return BinaryPacker::unpack(buffer, out_atom);
    }

    static std::size_t packedSize(const Atom& atom) noexcept {
        return BinaryPacker::packedSize(atom);
    }
};

} // namespace omnisync::network
