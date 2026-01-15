#include <iostream>
#include <cassert>
#include <vector>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

/**
 * Test 1: Single-user garbage collection
 */
void test_single_user_gc() {
    std::cout << "Test 1: Single-user GC..." << std::endl;
    
    Sequence doc(1);
    
    // Insert 100 characters
    for (int i = 0; i < 100; i++) {
        doc.localInsert(i, 'A' + (i % 26));
    }
    
    assert(doc.toString().length() == 100);
    assert(doc.getTombstoneCount() == 0);
    
    // Delete first 50 (create tombstones)
    for (int i = 0; i < 50; i++) {
        doc.localDelete(0);
    }
    
    assert(doc.toString().length() == 50);
    assert(doc.getTombstoneCount() == 50);
    
    // Run local GC with threshold = 60
    // This should remove tombstones with clock <= (current - 60)
    size_t removed = doc.garbageCollectLocal(60);
    
    std::cout << "  Removed " << removed << " tombstones" << std::endl;
    assert(removed > 0);  // Should remove at least some tombstones
    assert(doc.toString().length() == 50);  // Visible content unchanged
    assert(doc.getTombstoneCount() < 50);  // Some tombstones removed
    
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 2: Multi-user GC with stable frontier
 */
void test_multi_user_gc() {
    std::cout << "Test 2: Multi-user GC..." << std::endl;
    
    // Create 3 users
    Sequence user1(1), user2(2), user3(3);
    
    // User 1 creates initial text
    std::vector<Atom> ops;
    for (int i = 0; i < 20; i++) {
        Atom a = user1.localInsert(i, 'X');
        ops.push_back(a);
        user2.remoteMerge(a);
        user3.remoteMerge(a);
    }
    
    assert(user1.toString() == user2.toString());
    assert(user2.toString() == user3.toString());
    assert(user1.toString().length() == 20);
    
    // User 1 deletes first 10 chars
    std::vector<OpID> deletes;
    for (int i = 0; i < 10; i++) {
        OpID deleted = user1.localDelete(0);
        deletes.push_back(deleted);
        user2.remoteDelete(deleted);
        user3.remoteDelete(deleted);
    }
    
    assert(user1.toString() == user2.toString());
    assert(user2.toString() == user3.toString());
    assert(user1.toString().length() == 10);
    assert(user1.getTombstoneCount() == 10);
    
    // Collect peer states
    std::vector<VectorClock> peer_states = {
        user1.getVectorClock(),
        user2.getVectorClock(),
        user3.getVectorClock()
    };
    
    // Compute stable frontier
    VectorClock frontier = VectorClock::computeMinimum(peer_states);
    
    // Run GC on all users
    size_t r1 = user1.garbageCollect(frontier);
    size_t r2 = user2.garbageCollect(frontier);
    size_t r3 = user3.garbageCollect(frontier);
    
    std::cout << "  User1 removed: " << r1 << std::endl;
    std::cout << "  User2 removed: " << r2 << std::endl;
    std::cout << "  User3 removed: " << r3 << std::endl;
    
    // All users should remove same tombstones
    assert(r1 == r2 && r2 == r3);
    assert(r1 == 10);  // Should remove all 10 tombstones
    
    // All users still converge
    assert(user1.toString() == user2.toString());
    assert(user2.toString() == user3.toString());
    
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 3: GC safety (don't delete if peer hasn't seen it)
 */
void test_gc_safety() {
    std::cout << "Test 3: GC safety..." << std::endl;
    
    Sequence user1(1), user2(2);
    
    // User 1 creates and deletes
    Atom insert_atom = user1.localInsert(0, 'A');
    OpID deleted_id = user1.localDelete(0);
    
    // User 2 has NOT seen this yet (network delay)
    assert(user2.toString().empty());
    
    // User 1 tries to GC
    VectorClock empty_frontier(2);  // User 2 at clock 0
    empty_frontier.update(2, 0);
    size_t removed = user1.garbageCollect(empty_frontier);
    
    std::cout << "  Attempted GC removed: " << removed << " (should be 0)" << std::endl;
    
    // Should NOT delete tombstone (user2 hasn't seen it)
    assert(removed == 0);
    
    // Now user 2 receives the operations
    user2.remoteMerge(insert_atom);
    user2.remoteDelete(deleted_id);
    
    // Both should converge
    assert(user1.toString() == user2.toString());
    assert(user1.toString().empty());
    
    // Now we can safely GC
    std::vector<VectorClock> peer_states = {
        user1.getVectorClock(),
        user2.getVectorClock()
    };
    VectorClock frontier = VectorClock::computeMinimum(peer_states);
    size_t removed2 = user1.garbageCollect(frontier);
    
    std::cout << "  Safe GC removed: " << removed2 << " (should be 1)" << std::endl;
    assert(removed2 == 1);
    
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 4: Auto GC configuration
 */
void test_auto_gc() {
    std::cout << "Test 4: Auto GC..." << std::endl;
    
    Sequence doc(1);
    
    // Configure auto GC
    Sequence::GCConfig config;
    config.auto_gc_enabled = true;
    config.tombstone_threshold = 10;
    config.min_age_threshold = 5;
    doc.setGCConfig(config);
    
    // Create and delete characters
    for (int i = 0; i < 20; i++) {
        doc.localInsert(i, 'A');
    }
    
    // Delete to trigger auto GC
    for (int i = 0; i < 15; i++) {
        doc.localDelete(0);
    }
    
    // Auto GC should have triggered (threshold was 10 tombstones)
    assert(doc.getTombstoneCount() < 15);
    
    std::cout << "  Final tombstone count: " << doc.getTombstoneCount() << std::endl;
    std::cout << "  PASS" << std::endl;
}

/**
 * Test 5: Memory statistics
 */
void test_memory_stats() {
    std::cout << "Test 5: Memory statistics..." << std::endl;
    
    Sequence doc(1);
    
    for (int i = 0; i < 100; i++) {
        doc.localInsert(i, 'A' + (i % 26));
    }
    
    for (int i = 0; i < 50; i++) {
        doc.localDelete(0);
    }
    
    MemoryStats stats = doc.getMemoryStats();
    stats.print();
    
    assert(stats.atom_count > 0);
    assert(stats.tombstone_count == 50);
    assert(stats.total_bytes() > 0);
    
    std::cout << "  PASS" << std::endl;
}

int main() {
    std::cout << "=== OmniSync v1.3 Garbage Collection Tests ===" << std::endl << std::endl;
    
    try {
        test_single_user_gc();
        test_multi_user_gc();
        test_gc_safety();
        test_auto_gc();
        test_memory_stats();
        
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
