# OmniSync

OmniSync is a high-performance, header-only C++17 library for Conflict-free Replicated Data Types (CRDTs). It provides a robust engine for implementing real-time collaborative applications, decentralized synchronization, and eventual consistency in distributed systems.

## Overview

Traditional synchronization methods (like operational transformation) require a central server to resolve conflicts. OmniSync implements the Replicated Growable Array (RGA) algorithm, allowing multiple clients to edit shared state concurrently without a central authority. All merges are mathematically deterministic and guaranteed to converge.

### Key Features

- **Header-Only:** No complex build systems or link dependencies. Simply include the header.
- **Algorithm:** Uses RGA (Replicated Growable Array) for sequence data, ideal for text editors and ordered lists.
- **Concurrency:** Thread-safe atomic Lamport Clocks for causality tracking.
- **Optimization:** Updates and lookups use `std::unordered_map` for O(1) complexity in most operations.
- **Zero Dependencies:** Built on pure C++17 standard library (STL).
- **Network Agnostic:** Provides binary serialization helpers but lets you choose your transport layer (TCP/UDP/WebSocket).

## Installation

OmniSync is a header-only library. To use it, simply copy the `include/omnisync` directory into your project's include path.

```bash
cp -r OmniSync/include/omnisync /usr/local/include/
```

## Quick Start

### Basic Usage

```cpp
#include "omnisync/omnisync.hpp"
#include <iostream>

using namespace omnisync::core;

int main() {
    // initialize as Client ID: 1
    Sequence document(1);

    // Local Insertion (Time: 1)
    Atom a1 = document.localInsert(0, 'H');
    Atom a2 = document.localInsert(1, 'i');

    std::cout << document.toString() << std::endl; // Output: Hi
    
    return 0;
}
```

### Simulating Network Synchronization

```cpp
// Client A
Sequence docA(1);
Atom atomA = docA.localInsert(0, 'A');

// Client B
Sequence docB(2);
// Apply A's operation to B
docB.remoteMerge(atomA);

// B is now synchronized with A
assert(docB.toString() == "A");
```

## Architecture

### The Atom
The fundamental unit of data is the `Atom`. An Atom represents a single immutable event in the system history (e.g., "User 1 inserted 'A' at Time 10").
- **ID:** Unique identifier `{ClientID, LamportClock}`.
- **Origin:** The ID of the atom immediately preceding this one. This relative positioning allows the CRDT to survive index shifts.

### The Sequence
The `Sequence` class manages the linear history of Atoms. It implements the RGA conflict resolution logic:
1. **Sibling Conflict:** If two atoms claim the same parent, they are sorted by their unique IDs.
2. **Tombstones:** Deletions mark an atom as `is_deleted` rather than removing it from memory, preserving the causal history required for future merges.

## Building Examples

The repository includes a P2P Chat application demonstrating real-time synchronization over UDP.

```bash
# Compile
g++ -std=c++17 -I include examples/p2p_chat.cpp -o chat -lws2_32

# Run Instance 1
./chat 1 8000 8001

# Run Instance 2
./chat 2 8001 8000
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
