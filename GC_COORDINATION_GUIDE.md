# GC Coordination Guide

Author: Puneeth R  
Version: 1.4.0  
Last Updated: January 2026

## Overview

The GC Coordination system in OmniSync allows multiple peers to safely garbage collect tombstones in a distributed environment. Without coordination, a peer might delete a tombstone that another peer hasn't seen yet, causing data loss.

This guide explains how to use the `GCCoordinator` class to implement distributed garbage collection.

## The Problem

In a distributed CRDT system, each peer maintains its own copy of the data. When you delete a character, it creates a tombstone. These tombstones can't be immediately removed because other peers might not have seen all the operations yet.

**Example scenario:**

1. Peer A deletes character 'X' at position 5 (creates tombstone)
2. Peer B hasn't received this delete yet
3. If Peer A immediately GCs the tombstone, Peer B will never know about the deletion
4. Result: Data divergence

## The Solution: Stable Frontiers

The `GCCoordinator` uses **vector clocks** to compute a "stable frontier" - the minimum vector clock across all active peers. Any tombstone older than this frontier is safe to delete because all peers have seen it.

## Basic Usage

### 1. Create a Coordinator

```cpp
#include "omnisync/core/gc_coordinator.hpp"

// Create coordinator for this peer
uint64_t my_peer_id = 1;
GCCoordinator coordinator(my_peer_id);
```

### 2. Register Known Peers

```cpp
// When you discover other peers in your network
coordinator.registerPeer(2);  // Peer 2
coordinator.registerPeer(3);  // Peer 3
```

### 3. Update Peer States

Every time you receive a message from a peer, update their vector clock:

```cpp
// When you receive an atom from peer 2
Atom received_atom = /* ... from network ... */;
doc.remoteMerge(received_atom);

// Update coordinator with peer 2's vector clock
VectorClock peer2_clock = /* extract from message metadata */;
coordinator.updatePeerState(2, peer2_clock);
```

### 4. Perform Coordinated GC

```cpp
// Periodically check if GC should run
if (coordinator.shouldTriggerGC()) {
    VectorClock frontier = coordinator.computeStableFrontier();
    size_t removed = doc.garbageCollect(frontier);
    
    std::cout << "Removed " << removed << " tombstones\n";
}

// Or use the all-in-one method
size_t removed = coordinator.performCoordinatedGC(doc);
```

### 5. Update Your Own Vector Clock

After making local changes, update the coordinator:

```cpp
// After local insert or delete
Atom new_atom = doc.localInsert(pos, 'A');
coordinator.updateMyVectorClock(doc.getVectorClock());
```

## Advanced Configuration

### Custom GC Timing

```cpp
GCCoordinator::Config config;
config.heartbeat_interval_ms = 5000;   // Send heartbeat every 5 seconds
config.peer_timeout_ms = 30000;        // Consider peer dead after 30 seconds
config.gc_interval_ms = 60000;         // Try GC every 60 seconds
config.min_peers_for_gc = 2;           // Require at least 2 peers before GC
config.auto_gc_enabled = true;         // Enable automatic GC triggering

GCCoordinator coordinator(my_peer_id, config);
```

### Heartbeat Protocol

To keep peer states fresh, implement a heartbeat:

```cpp
// Periodically send your vector clock to all peers
void sendHeartbeats() {
    coordinator.sendHeartbeat([](uint64_t peer_id, const VectorClock& vc) {
        // Send vc to peer_id over your network
        network.sendTo(peer_id, vc);
    });
}

// When you receive a heartbeat from a peer
void onHeartbeatReceived(uint64_t peer_id, const VectorClock& vc) {
    coordinator.processHeartbeat(peer_id, vc);
}
```

### Peer Lifecycle Management

```cpp
// When a peer joins
coordinator.registerPeer(new_peer.id);

// When a peer leaves gracefully
coordinator.removePeer(leaving_peer.id);

// Peer timeouts are handled automatically
// Peers that don't send heartbeats are marked inactive
```

## Complete Example: P2P Chat with GC

```cpp
#include "omnisync/omnisync.hpp"
#include <thread>
#include <chrono>

using namespace omnisync::core;

class DistributedEditor {
    Sequence doc;
    GCCoordinator gc;
    uint64_t my_id;
    
public:
    DistributedEditor(uint64_t id) 
        : doc(id), gc(id), my_id(id) {}
    
    void addPeer(uint64_t peer_id) {
        gc.registerPeer(peer_id);
    }
    
    void onMessageReceived(uint64_t from_peer, const Atom& atom) {
        // Merge the atom
        doc.remoteMerge(atom);
        
        // Update peer's vector clock
        // In real code, you'd extract this from message metadata
        gc.updatePeerState(from_peer, doc.getVectorClock());
    }
    
    void onLocalEdit(size_t pos, char ch) {
        Atom atom = doc.localInsert(pos, ch);
        gc.updateMyVectorClock(doc.getVectorClock());
        
        // Broadcast atom to all peers
        // broadcast(atom);
    }
    
    void periodicMaintenance() {
        // This runs in a background thread
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            // Try coordinated GC
            if (gc.shouldTriggerGC()) {
                size_t removed = gc.performCoordinatedGC(doc);
                if (removed > 0) {
                    std::cout << "GC removed " << removed << " tombstones\n";
                }
            }
        }
    }
};
```

## Safety Guarantees

The GC coordinator provides these safety properties:

1. **Monotonicity**: The stable frontier never goes backward
2. **Liveness**: If all peers are active, GC will eventually run
3. **Safety**: No tombstone is deleted unless all active peers have seen it
4. **Partition Tolerance**: Inactive peers are excluded from frontier computation

## Performance Considerations

- **Heartbeat frequency**: More frequent heartbeats mean fresher GC, but more network traffic
- **Peer timeout**: Longer timeouts are safer during network issues, but delay GC
- **Min peers threshold**: Set higher for critical applications, lower for small networks

## Troubleshooting

### GC Never Runs

**Check:**
- Are peers actually being registered?
- Are you updating peer vector clocks?
- Is `shouldTriggerGC()` returning true?
- Check `getActivePeers()` to see if peers are timing out

### Too Aggressive GC

**Solution:**
- Increase `peer_timeout_ms`
- Increase `min_peers_for_gc`
- Add delays between GC runs

### Memory Still Growing

**Check:**
- Is auto-GC enabled on the Sequence itself?
- Are you actually calling `garbageCollect()`?
- Check tombstone count with `getTombstoneCount()`

## Further Reading

- CRDT paper: "Conflict-free Replicated Data Types"
- Vector Clocks: Lamport's "Time, Clocks, and the Ordering of Events"
- OmniSync source: `include/omnisync/core/gc_coordinator.hpp`

---

**Questions or issues?** Check the test suite in `tests/gc_coord_test.cpp` for more examples.
