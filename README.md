# OmniSync: The Universal C++ CRDT Engine

## The Mission
To build the **standard, high-performance C++ library** for Conflict-free Replicated Data Types (CRDTs). 
We aim to solve the "Global State Sync" problem by providing a native, dependency-free engine that allows any C++ application (Game, GUI, Embedded) to support real-time collaboration and offline-first capabilities.

## The Battlefield (Market Gap Analysis)
**Status as of 2026:**
*   **JavaScript:** Dominated by [Yjs](https://github.com/yjs/yjs) and [Automerge](https://automerge.org/). (Slow for massive data, restricted to JS runtimes).
*   **Rust:** Automerge is rewriting in Rust. Good, but heavy integration for pure C++ projects.
*   **C++:** **EMPTY.**  
    *   There is no `std::crdt`.
    *   There is no "Boost.CRDT".
    *   Existing C++ repos are either abandoned research projects, partial implementations (only simple counters), or wrappers around Rust/Go.
    *   *Verdict:* **A massive gap exists for a header-only, high-performance C++17/20 CRDT library.**

## The Tech Stack (Planned)
*   **Language:** C++ 20 (for Concepts and Modules).
*   **Algorithm:** RGA (Replicated Growable Array) for Sequence/Text.
*   **Timekeeping:** Lamport Clocks / Vector Clocks.
*   **Memory:** Custom Block Allocator (to beat typical Linked List cache-misses).
*   **Format:** Binary-optimized (Zero-Copy serialization).

## Roadmap (The "Resume Builder" Arc)
1.  **Phase 1: The Atom:** Define the core `struct` and ID generation.
2.  **Phase 2: The Timeline:** Implement `LamportClock` to order events.
3.  **Phase 3: The Merge:** Implement `insert()` and `delete()` with conflict resolution.
4.  **Phase 4: The Proof:** A simple CLI demo where two "users" edit text out of order, and it converges.

---
*Started: January 2026*
