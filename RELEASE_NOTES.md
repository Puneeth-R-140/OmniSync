# OmniSync Release Notes

## Scope and Disclaimer

These notes summarize the releases that matter for study and maintenance. Performance numbers below were measured in this repository on the stated test inputs and build environment. They are not universal guarantees and will vary with hardware, compiler, optimization level, operating system, and workload shape.

## v1.4.0 - Stability Master

Release date: January 20, 2026

### Highlights

- 24-hour stability testing framework with no memory leaks detected in the recorded run.
- Distributed garbage-collection coordination through `GCCoordinator`.
- Enhanced memory profiling, including GC timing statistics.

### Why it mattered

This release moved OmniSync from a working collaborative engine to a long-running system that could be discussed credibly in production terms. The main focus was keeping convergence intact while proving the implementation can run for extended periods without unbounded growth.

### Notable additions

- `tests/stability_test.cpp`
- `tests/gc_coord_test.cpp`
- `tests/profiling_test.cpp`
- GC timing and memory-stat integration in `Sequence`

### Verification notes

- Stability test: 24-hour continuous run, 382,960 operations, no leaks detected in the recorded session.
- GC coordination tests: peer lifecycle, frontier computation, and partition/rejoin handling verified by test output.
- Memory profiling: GC timing metrics surfaced in `MemoryStats`.

## v1.3.0 - Memory Master

Release date: January 15, 2026

### Highlights

- Safe tombstone deletion using a vector-clock frontier.
- Orphan buffer limits with eviction policy.
- Memory profiling utilities.
- Automatic and manual garbage-collection modes.

### Why it mattered

v1.2 solved bandwidth. v1.3 solved the long-running memory problem so the engine could remain stable when edits and deletes continue for a long session.

### Notable additions

- `Sequence::garbageCollect()`
- `Sequence::garbageCollectLocal()`
- `Sequence::setGCConfig()` / `getGCConfig()`
- `Sequence::setOrphanConfig()` / `getOrphanConfig()`
- `Sequence::getMemoryStats()`

### Verification notes

- GC tests cover single-user, multi-user, safety, auto-GC, and memory-stat behavior.
- Memory use becomes bounded once tombstones are safely reclaimed.

## v1.2.0 - Compression Improvement

Release date: January 10, 2026

### Highlights

- Delta synchronization using vector clocks.
- Variable-length encoding for atom identifiers.
- Backward-compatible persistence upgrade from v1.0 to v1.2.

### Why it mattered

This was the first release that made OmniSync efficient enough to discuss as a real collaboration engine rather than only a correctness demo. It reduced bandwidth enough that the later placement showcase became practical.

### Notable additions

- `Sequence::getVectorClock()`
- `Sequence::mergeVectorClock()`
- `Sequence::getDelta()`
- `Sequence::applyDelta()`
- VLE packing utilities in the network layer

### Verification notes

- VLE compression was measured at 82.4% on the repository test workload.
- Delta sync reduced bandwidth substantially on established documents.
- Combined delta + VLE reduction reached 98.2% in the documented scenario.

## Current Study Notes

- The project is a header-only C++17 CRDT library based on an RGA-style sequence.
- The current demo and test suite should be treated as repository-specific validation, not a universal benchmark promise.
- When presenting results, always say how they were measured and on which workload.

## References

- [README](README.md)
- [BENCHMARKS](BENCHMARKS.md)
- [ROADMAP](ROADMAP.md)
- [notes/00_MASTER_INDEX](notes/00_MASTER_INDEX.md)
