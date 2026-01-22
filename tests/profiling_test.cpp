#include <iostream>
#include <cassert>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

/**
 * Test enhanced memory profiling with GC stats
 */
int main() {
    std::cout << "=== Enhanced Memory Profiling Test ===" << std::endl << std::endl;
    
    Sequence doc(1);
    
    // Create atoms
    std::cout << "Creating 100 atoms..." << std::endl;
    for (int i = 0; i < 100; i++) {
        doc.localInsert(i, 'A' + (i % 26));
    }
    
    // Delete 50 to create tombstones
    std::cout << "Deleting 50 atoms (creating tombstones)..." << std::endl;
    for (int i = 0; i < 50; i++) {
        doc.localDelete(0);
    }
    
    // Perform GC multiple times
    std::cout << "\nPerforming GC 5 times..." << std::endl;
    for (int i = 0; i < 5; i++) {
        size_t removed = doc.garbageCollectLocal(10);
        std::cout << "  GC run " << (i+1) << ": removed " << removed << " tombstones" << std::endl;
    }
    
    // Get stats
    std::cout << "\n";
    MemoryStats stats = doc.getMemoryStats();
    stats.print();
    
    // Verify GC stats were tracked
    assert(stats.gc_stats.total_gc_runs == 5);
    assert(stats.gc_stats.total_tombstones_removed > 0);
    assert(stats.gc_stats.avg_gc_time_us >= 0.0);  // Can be 0 for very fast operations
    assert(stats.gc_stats.max_gc_time_us >= 0);
    assert(stats.gc_stats.last_gc_time_us >= 0);   // Can be 0 for very fast operations
    
    std::cout << "\n=== TEST PASSED ===" << std::endl;
    std::cout << "\nGC Performance Summary:" << std::endl;
    std::cout << "  Average GC time: " << stats.gc_stats.avg_gc_time_us << " μs" << std::endl;
    std::cout << "  Peak GC time: " << stats.gc_stats.max_gc_time_us << " μs" << std::endl;
    std::cout << "  Total time in GC: " << stats.gc_stats.total_gc_time_us / 1000.0 << " ms" << std::endl;
    
    return 0;
}
