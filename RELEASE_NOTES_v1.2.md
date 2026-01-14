# OmniSync v1.2 Release Notes

**Release Date**: January 10, 2026  
**Version**: 1.2.0 "Compression Champion"

---

## Summary

Version 1.2 introduces two major performance optimizations that work together to significantly reduce network bandwidth usage. The combination of delta synchronization and variable-length encoding reduces bandwidth requirements by 98% compared to naive full-state synchronization.

---

## Major Changes

### Delta Synchronization

Added vector clock-based delta sync that allows peers to exchange only the operations they're missing, rather than the entire document state. This is particularly effective for large documents where only a small portion has changed.

**Implementation:**
- New `VectorClock` class tracks causal dependencies
- `Sequence::getDelta()` computes minimal operation set for sync
- `Sequence::applyDelta()` applies received operations

**Performance:**
- Typical reduction: 90% for established documents
- Initial sync overhead: ~50% (due to vector clock exchange)

### Variable-Length Encoding (VLE)

Implemented LEB128 encoding for operation identifiers. Since most collaborative sessions involve relatively few users and temporally local edits, OpIDs typically use much less than 8 bytes per field.

**Implementation:**
- New `VLEEncoding` class provides encoding/decoding utilities
- `VLEPacker` class replaces fixed-size serialization
- Backward compatible with v1.0 (both packers available)

**Results:**
- Average atom size: 6 bytes (down from 34 bytes)
- Compression ratio: 82.4% for typical workloads
- Worst case (4B+ operations): Still 35% reduction

### Combined Effect

When both optimizations are enabled:
- Example: 1000-atom document, 100 new operations
- Naive approach: 34,000 bytes
- Delta + VLE: 600 bytes
- Net reduction: 98.2%

---

## API Changes

### New Methods

```cpp
// Delta synchronization
const VectorClock& Sequence::getVectorClock() const;
void Sequence::mergeVectorClock(const VectorClock& peer_clock);
std::vector<Atom> Sequence::getDelta(const VectorClock& peer_state) const;
void Sequence::applyDelta(const std::vector<Atom>& delta);

// VLE packing (new class)
std::vector<uint8_t> VLEPacker::pack(const Atom& atom);
bool VLEPacker::unpack(const std::vector<uint8_t>& buffer, Atom& out_atom);
size_t VLEPacker::packedSize(const Atom& atom);
```

### Modified Methods

```cpp
// Previous signature (still works)
void Sequence::save(std::ostream& out) const;

// Now includes vector clock in serialization
// File format version bumped from 1 to 2
// Version 1 files can still be loaded
```

### Deprecated

None. All v1.0 APIs remain functional.

---

## File Structure Changes

### New Files
- `include/omnisync/core/vector_clock.hpp` - Causal dependency tracking
- `include/omnisync/network/vle_encoding.hpp` - LEB128 encoder/decoder
- `tests/delta_sync_test.cpp` - Delta sync verification
- `tests/vle_compression_test.cpp` - Compression ratio verification

### Modified Files
- `include/omnisync/core/sequence.hpp` - Delta sync methods added
- `include/omnisync/network/binary_packer.hpp` - VLEPacker added
- `include/omnisync/omnisync.hpp` - Version bump to 1.2.0

---

## Testing

All existing tests continue to pass:

```bash
./fuzz.exe        # 2,500 random operations, 100% convergence
./save_test.exe   # Persistence verified
```

New tests added:

```bash
./delta_test.exe  # Delta sync correctness
./vle_test.exe    # Compression ratio measurement
```

See `test_results_*.txt` for detailed output.

---

## Performance Benchmarks

### VLE Compression Test

**Setup:** 500 atoms, 5 users, 100 edits each

| Metric | Fixed Encoding | VLE Encoding |
|--------|----------------|--------------|
| Total Size | 17,000 bytes | 3,000 bytes |
| Per Atom | 34 bytes | 6 bytes |
| Reduction | - | 82.4% |

**Worst case** (UINT32_MAX client ID and clock):
- Fixed: 34 bytes
- VLE: 22 bytes  
- Still 35% smaller

### Delta Sync Test

**Small document** (11 atoms total, 6 new):
- Full sync: 11 operations
- Delta sync: 6 operations
- Reduction: 45%

**Larger document** (1000 atoms total, 100 new):
- Full sync: 1000 operations
- Delta sync: 100 operations
- Reduction: 90%

### Combined

For the 1000-atom scenario with VLE:
- Naive: 1000 × 34 = 34,000 bytes
- Delta only: 100 × 34 = 3,400 bytes (90% reduction)
- Delta + VLE: 100 × 6 = 600 bytes (98.2% reduction)

---

## Migration Guide

### From v1.0 to v1.2

**No breaking changes.** Your v1.0 code will compile and run without modification.

**To use new features:**

```cpp
// Optional: Use delta sync for efficiency
VectorClock peer_state = get_from_peer();
std::vector<Atom> delta = doc.getDelta(peer_state);
send_to_peer(delta); // Only new operations

// Optional: Use VLE for smaller packets
for (const auto& atom : delta) {
    auto bytes = VLEPacker::pack(atom); // Was BinaryPacker::pack
    send_over_network(bytes);
}
```

**Persistence compatibility:**
- v1.2 can read v1.0 save files
- v1.0 cannot read v1.2 save files (vector clock not understood)
- If compatibility needed, continue using BinaryPacker

---

## Known Issues

### Delta Sync Limitations

1. **No automatic catchup:** If a peer misses many operations, full sync may be more efficient. No automatic threshold detection yet.

2. **Vector clock overhead:** Small documents (< 100 atoms) may see increased bandwidth due to vector clock metadata.

### VLE Limitations

1. **Endianness:** Assumes little-endian for stream I/O. Works on x86/ARM, may need adjustment for big-endian systems.

2. **No RLE:** Sequential inserts from same user still create separate atoms. Run-length encoding planned for v1.3.

### General

1. **No garbage collection:** Long-running sessions will accumulate tombstones. Safe GC planned for v1.3.

2. **List-based storage:** Performance degrades for very large documents (>100MB). Rope structure planned for v1.5.

---

## Comparison with Other Libraries

**Bandwidth efficiency** (claimed vs verified):

| Library | Technology | Verified |
|---------|------------|----------|
| OmniSync v1.2 | Delta + VLE | Yes (our tests) |
| Yjs | Delta + encoding | Not tested |
| Automerge | Columnar + delta | Not tested |

We have not run direct benchmarks against Yjs or Automerge. The 98% figure is based on our own measurements against a naive baseline. Comparative benchmarks are planned for v1.5.

---

## Credits

**Algorithms:**
- RGA: Roh et al., 2011
- LEB128: Used in Protocol Buffers, DWARF, WebAssembly
- Vector Clocks: Lamport, 1978; Mattern, 1988

**Inspiration:**
- Yjs by Kevin Jahns
- Automerge by Martin Kleppmann et al.
- Diamond Types by Joseph Gentle

---

## Next Steps

**v1.3** (planned for late January):
- Safe garbage collection using vector clock frontier
- 24-hour stability fuzz test
- Memory usage profiling

**v1.5** (Q1 2026):
- Direct performance comparison with Yjs
- Rope data structure for scalability
- Throughput benchmarks (ops/second)

**v2.0** (Q3 2026):
- Rich text support (formatting marks)
- WASM bindings for JavaScript interop
- Additional CRDT types (Map, Set)

---

## License

MIT License - See LICENSE file for details.

---

## Changelog

**Added:**
- VectorClock class for causal tracking
- Delta synchronization (getDelta/applyDelta)
- VLE encoding (LEB128-based compression)
- VLEPacker for efficient serialization
- Comprehensive benchmark suite

**Changed:**
- Sequence now tracks VectorClock internally
- Save file format version 2 (backward compatible)
- OpID hash implementation (no API change)

**Fixed:**
- None (no bugs fixed in this release)

**Deprecated:**
- None

---

**Questions or issues?** Please file a GitHub issue or consult the documentation in BENCHMARKS.md and ROADMAP.md.
