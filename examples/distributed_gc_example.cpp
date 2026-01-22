/**
 * Distributed GC Example
 * 
 * This example demonstrates how to use GCCoordinator for safe distributed
 * garbage collection across multiple peers.
 * 
 * Author: Puneeth R
 * Date: January 2026
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <random>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

/**
 * Simulated peer in a distributed system
 */
class Peer {
private:
    uint64_t id;
    Sequence doc;
    GCCoordinator gc_coordinator;
    std::vector<Peer*> known_peers;
    
public:
    Peer(uint64_t peer_id) 
        : id(peer_id)
        , doc(peer_id)
        , gc_coordinator(peer_id) 
    {
        // Configure GC for demonstration
        GCCoordinator::Config config;
        config.gc_interval_ms = 5000;      // GC every 5 seconds
        config.peer_timeout_ms = 15000;    // 15 second timeout
        config.auto_gc_enabled = true;
        
        gc_coordinator = GCCoordinator(peer_id, config);
        
        std::cout << "[Peer " << id << "] Created\n";
    }
    
    void connectToPeer(Peer* other_peer) {
        known_peers.push_back(other_peer);
        gc_coordinator.registerPeer(other_peer->getId());
        std::cout << "[Peer " << id << "] Connected to Peer " 
                  << other_peer->getId() << "\n";
    }
    
    void localInsert(size_t pos, char ch) {
        Atom atom = doc.localInsert(pos, ch);
        
        // Update own vector clock in coordinator
        gc_coordinator.updateMyVectorClock(doc.getVectorClock());
        
        // Broadcast to all peers
        for (auto* peer : known_peers) {
            peer->receiveAtom(id, atom);
        }
    }
    
    void localDelete(size_t pos) {
        if (doc.toString().empty()) return;
        
        OpID deleted = doc.localDelete(pos);
        gc_coordinator.updateMyVectorClock(doc.getVectorClock());
        
        // Broadcast to all peers
        for (auto* peer : known_peers) {
            peer->receiveDelete(id, deleted);
        }
    }
    
    void receiveAtom(uint64_t from_peer, const Atom& atom) {
        doc.remoteMerge(atom);
        
        // Update coordinator with peer's latest state
        gc_coordinator.updatePeerState(from_peer, doc.getVectorClock());
    }
    
    void receiveDelete(uint64_t from_peer, const OpID& deleted) {
        doc.remoteDelete(deleted);
        gc_coordinator.updatePeerState(from_peer, doc.getVectorClock());
    }
    
    void tryGarbageCollection() {
        if (gc_coordinator.shouldTriggerGC()) {
            size_t removed = gc_coordinator.performCoordinatedGC(doc);
            if (removed > 0) {
                std::cout << "[Peer " << id << "] GC removed " 
                          << removed << " tombstones\n";
            }
        }
    }
    
    void printStatus() {
        MemoryStats stats = doc.getMemoryStats();
        std::cout << "[Peer " << id << "] "
                  << "Content: \"" << doc.toString() << "\" | "
                  << "Atoms: " << stats.atom_count << " | "
                  << "Tombstones: " << stats.tombstone_count << " | "
                  << "Memory: " << stats.total_bytes() / 1024 << " KB\n";
    }
    
    uint64_t getId() const { return id; }
    std::string getContent() const { return doc.toString(); }
    size_t getTombstoneCount() const { return doc.getTombstoneCount(); }
};

/**
 * Main demonstration
 */
int main() {
    std::cout << "=== Distributed GC Coordination Example ===\n\n";
    
    // Create 3 peers
    Peer peer1(1);
    Peer peer2(2);
    Peer peer3(3);
    
    // Connect them in a mesh network
    std::cout << "\nConnecting peers...\n";
    peer1.connectToPeer(&peer2);
    peer1.connectToPeer(&peer3);
    peer2.connectToPeer(&peer1);
    peer2.connectToPeer(&peer3);
    peer3.connectToPeer(&peer1);
    peer3.connectToPeer(&peer2);
    
    // Simulate collaborative editing
    std::cout << "\n--- Phase 1: Collaborative Editing ---\n";
    
    peer1.localInsert(0, 'H');
    peer1.localInsert(1, 'e');
    peer1.localInsert(2, 'l');
    peer1.localInsert(3, 'l');
    peer1.localInsert(4, 'o');
    
    peer2.localInsert(5, ' ');
    peer2.localInsert(6, 'W');
    peer2.localInsert(7, 'o');
    peer2.localInsert(8, 'r');
    peer2.localInsert(9, 'l');
    peer2.localInsert(10, 'd');
    
    std::cout << "\nAfter inserts:\n";
    peer1.printStatus();
    peer2.printStatus();
    peer3.printStatus();
    
    // Verify convergence
    bool converged = (peer1.getContent() == peer2.getContent()) && 
                     (peer2.getContent() == peer3.getContent());
    std::cout << "\nConvergence: " << (converged ? "YES" : "NO") << "\n";
    
    // Create some tombstones
    std::cout << "\n--- Phase 2: Creating Tombstones ---\n";
    
    peer1.localDelete(5);  // Delete space
    peer2.localDelete(0);  // Delete 'H'
    peer3.localDelete(0);  // Delete 'e'
    
    std::cout << "\nAfter deletions:\n";
    peer1.printStatus();
    peer2.printStatus();
    peer3.printStatus();
    
    // Try GC before coordination
    std::cout << "\n--- Phase 3: Attempt GC (should wait) ---\n";
    peer1.tryGarbageCollection();
    peer2.tryGarbageCollection();
    peer3.tryGarbageCollection();
    
    // Simulate some time passing and send heartbeats
    std::cout << "\n--- Phase 4: Wait and Retry GC ---\n";
    std::cout << "Waiting 6 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    peer1.tryGarbageCollection();
    peer2.tryGarbageCollection();
    peer3.tryGarbageCollection();
    
    std::cout << "\nAfter GC:\n";
    peer1.printStatus();
    peer2.printStatus();
    peer3.printStatus();
    
    // Final convergence check
    converged = (peer1.getContent() == peer2.getContent()) && 
                (peer2.getContent() == peer3.getContent());
    
    std::cout << "\n=== Final Results ===\n";
    std::cout << "All peers converged: " << (converged ? "YES" : "NO") << "\n";
    std::cout << "Final content: \"" << peer1.getContent() << "\"\n";
    
    // Check that tombstones were cleaned up consistently
    bool same_tombstones = (peer1.getTombstoneCount() == peer2.getTombstoneCount()) &&
                           (peer2.getTombstoneCount() == peer3.getTombstoneCount());
    std::cout << "Tombstone counts match: " << (same_tombstones ? "YES" : "NO") << "\n";
    
    return 0;
}
