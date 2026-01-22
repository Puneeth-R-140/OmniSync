/**
 * Basic Usage Example
 * 
 * Demonstrates core OmniSync features: local operations, remote merging,
 * convergence, and basic GC.
 * 
 * Author: Puneeth R
 */

#include <iostream>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

int main() {
    std::cout << "=== OmniSync Basic Example ===\n\n";
    
    // Create two users
    Sequence user1(1);
    Sequence user2(2);
    
    std::cout << "Two users created (User 1 and User 2)\n\n";
    
    // User 1 types "Hello"
    std::cout << "User 1 types: ";
    Atom op1 = user1.localInsert(0, 'H');
    Atom op2 = user1.localInsert(1, 'e');
    Atom op3 = user1.localInsert(2, 'l');
    Atom op4 = user1.localInsert(3, 'l');
    Atom op5 = user1.localInsert(4, 'o');
    
    std::cout << user1.toString() << "\n";
    
    // User 2 types " World"
    std::cout << "User 2 types: ";
    Atom op6 = user2.localInsert(0, ' ');
    Atom op7 = user2.localInsert(1, 'W');
    Atom op8 = user2.localInsert(2, 'o');
    Atom op9 = user2.localInsert(3, 'r');
    Atom op10 = user2.localInsert(4, 'l');
    Atom op11 = user2.localInsert(5, 'd');
    
    std::cout << user2.toString() << "\n\n";
    
    // Sync operations
    std::cout << "--- Syncing operations ---\n";
    
    // User 2 receives User 1's operations
    user2.remoteMerge(op1);
    user2.remoteMerge(op2);
    user2.remoteMerge(op3);
    user2.remoteMerge(op4);
    user2.remoteMerge(op5);
    
    // User 1 receives User 2's operations
    user1.remoteMerge(op6);
    user1.remoteMerge(op7);
    user1.remoteMerge(op8);
    user1.remoteMerge(op9);
    user1.remoteMerge(op10);
    user1.remoteMerge(op11);
    
    std::cout << "After sync:\n";
    std::cout << "  User 1: " << user1.toString() << "\n";
    std::cout << "  User 2: " << user2.toString() << "\n";
    
    // Check convergence
    bool converged = (user1.toString() == user2.toString());
    std::cout << "\nConvergence: " << (converged ? "YES" : "NO") << "\n\n";
    
    // Demonstrate deletion
    std::cout << "--- User 1 deletes 'o' at position 4 ---\n";
    OpID deleted = user1.localDelete(4);
    user2.remoteDelete(deleted);
    
    std::cout << "After deletion:\n";
    std::cout << "  User 1: " << user1.toString() << "\n";
    std::cout << "  User 2: " << user2.toString() << "\n";
    
    // Show memory stats
    std::cout << "\n--- Memory Statistics ---\n";
    MemoryStats stats = user1.getMemoryStats();
    std::cout << "Atoms: " << stats.atom_count << "\n";
    std::cout << "Tombstones: " << stats.tombstone_count << "\n";
    std::cout << "Memory: " << stats.total_bytes() / 1024 << " KB\n";
    
    // Demonstrate GC
    std::cout << "\n--- Garbage Collection ---\n";
    size_t removed = user1.garbageCollectLocal(100);
    std::cout << "Removed " << removed << " tombstones\n";
    
    std::cout << "\nFinal memory: " << user1.getMemoryStats().total_bytes() / 1024 << " KB\n";
    
    return 0;
}
