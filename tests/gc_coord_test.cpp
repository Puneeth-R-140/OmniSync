#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include <chrono>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

/**
 * Test 1: Basic peer registration and tracking
 */
void test_peer_management() {
    std::cout << "Test 1: Peer management..." << std::endl;
    
    GCCoordinator coordinator(1);  // Peer 1
    
    // Register peers
    coordinator.registerPeer(2);
    coordinator.registerPeer(3);
    
    assert(coordinator.getPeerCount() == 2);
    assert(coordinator.getActivePeerCount() == 0);  // No updates yet
    
    // Update peer states
    VectorClock vc1(2);
    vc1.tick();
    coordinator.updatePeerState(2, vc1);
    
    VectorClock vc2(3);
    vc2.tick();
    coordinator.updatePeerState(3, vc2);
    
    assert(coordinator.getActivePeerCount() == 2);
    
    // Remove peer
    coordinator.removePeer(2);
    assert(coordinator.getPeerCount() == 1);
    
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 2: Stable frontier computation
 */
void test_stable_frontier() {
    std::cout << "Test 2: Stable frontier computation..." << std::endl;
    
    // Create 3 users with coordinator
    Sequence user1(1), user2(2), user3(3);
    GCCoordinator gc_coord(1);
    
    gc_coord.registerPeer(2);
    gc_coord.registerPeer(3);
    
    // User 1 creates text
    for (int i = 0; i < 10; i++) {
        Atom a = user1.localInsert(i, 'A' + i);
        user2.remoteMerge(a);
        user3.remoteMerge(a);
    }
    
    // Update coordinator with all peer states
    gc_coord.updateMyVectorClock(user1.getVectorClock());
    gc_coord.updatePeerState(2, user2.getVectorClock());
    gc_coord.updatePeerState(3, user3.getVectorClock());
    
    // Compute frontier
    VectorClock frontier = gc_coord.computeStableFrontier();
    
    // Frontier should be at minimum of all clocks
    assert(frontier.get(1) >= 10);  // User 1 did 10 operations
    
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 3: Coordinated GC across peers
 */
void test_coordinated_gc() {
    std::cout << "Test 3: Coordinated GC..." << std::endl;
    
    // Create 3 users
    Sequence user1(1), user2(2), user3(3);
    GCCoordinator gc1(1), gc2(2), gc3(3);
    
    // Register peers in each coordinator
    gc1.registerPeer(2);
    gc1.registerPeer(3);
    gc2.registerPeer(1);
    gc2.registerPeer(3);
    gc3.registerPeer(1);
    gc3.registerPeer(2);
    
    // Create and delete content
    for (int i = 0; i < 20; i++) {
        Atom a = user1.localInsert(i, 'X');
        user2.remoteMerge(a);
        user3.remoteMerge(a);
    }
    
    // Delete first 10
    for (int i = 0; i < 10; i++) {
        OpID deleted = user1.localDelete(0);
        user2.remoteDelete(deleted);
        user3.remoteDelete(deleted);
    }
    
    assert(user1.getTombstoneCount() == 10);
    assert(user2.getTombstoneCount() == 10);
    assert(user3.getTombstoneCount() == 10);
    
    // Update coordinators with current states
    gc1.updateMyVectorClock(user1.getVectorClock());
    gc1.updatePeerState(2, user2.getVectorClock());
    gc1.updatePeerState(3, user3.getVectorClock());
    
    gc2.updateMyVectorClock(user2.getVectorClock());
    gc2.updatePeerState(1, user1.getVectorClock());
    gc2.updatePeerState(3, user3.getVectorClock());
    
    gc3.updateMyVectorClock(user3.getVectorClock());
    gc3.updatePeerState(1, user1.getVectorClock());
    gc3.updatePeerState(2, user2.getVectorClock());
    
    // Perform coordinated GC
    size_t r1 = gc1.performCoordinatedGC(user1);
    size_t r2 = gc2.performCoordinatedGC(user2);
    size_t r3 = gc3.performCoordinatedGC(user3);
    
    std::cout << "  User1 removed: " << r1 << std::endl;
    std::cout << "  User2 removed: " << r2 << std::endl;
    std::cout << "  User3 removed: " << r3 << std::endl;
    
    // All should remove same amount
    assert(r1 == r2 && r2 == r3);
    assert(r1 == 10);  // Should remove all 10 tombstones
    
    // Verify convergence still holds
    assert(user1.toString() == user2.toString());
    assert(user2.toString() == user3.toString());
    
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 4: Auto-GC triggering
 */
void test_auto_gc_trigger() {
    std::cout << "Test 4: Auto-GC triggering..." << std::endl;
    
    GCCoordinator::Config config;
    config.auto_gc_enabled = true;
    config.gc_interval_ms = 100;  // 100ms for fast testing
    config.min_peers_for_gc = 1;
    
    GCCoordinator gc_coord(1, config);
    gc_coord.registerPeer(2);
    
    // Update peer state
    VectorClock vc(2);
    vc.tick();
    gc_coord.updatePeerState(2, vc);
    
    // Should not trigger immediately
    assert(!gc_coord.shouldTriggerGC());
    
    // Wait for interval
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Should trigger now
    assert(gc_coord.shouldTriggerGC());
    
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 5: Peer timeout handling
 */
void test_peer_timeout() {
    std::cout << "Test 5: Peer timeout..." << std::endl;
    
    GCCoordinator::Config config;
    config.peer_timeout_ms = 100;  // 100ms timeout
    
    GCCoordinator gc_coord(1, config);
    gc_coord.registerPeer(2);
    
    // Update peer state
    VectorClock vc(2);
    vc.tick();
    gc_coord.updatePeerState(2, vc);
    
    assert(gc_coord.getActivePeerCount() == 1);
    
    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Peer should now be considered inactive
    assert(gc_coord.getActivePeerCount() == 0);
    
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 6: Heartbeat mechanism
 */
void test_heartbeat() {
    std::cout << "Test 6: Heartbeat mechanism..." << std::endl;
    
    GCCoordinator gc1(1), gc2(2);
    
    gc1.registerPeer(2);
    gc2.registerPeer(1);
    
    // Simulate heartbeat exchange
    VectorClock vc1(1);
    vc1.tick();
    gc1.updateMyVectorClock(vc1);
    
    VectorClock vc2(2);
    vc2.tick();
    gc2.updateMyVectorClock(vc2);
    
    // Process heartbeats
    gc1.processHeartbeat(2, vc2);
    gc2.processHeartbeat(1, vc1);
    
    assert(gc1.getActivePeerCount() == 1);
    assert(gc2.getActivePeerCount() == 1);
    
    std::cout << "  PASS" << std::endl;
}

int main() {
    std::cout << "=== OmniSync GC Coordinator Tests ===" << std::endl << std::endl;
    
    try {
        test_peer_management();
        test_stable_frontier();
        test_coordinated_gc();
        test_auto_gc_trigger();
        test_peer_timeout();
        test_heartbeat();
        
        std::cout << std::endl << "=== ALL TESTS PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}
