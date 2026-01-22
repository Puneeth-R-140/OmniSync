/**
 * Memory Profiling Example
 * 
 * Demonstrates v1.4 memory profiling capabilities including GC performance
 * tracking and memory statistics.
 * 
 * Author: Puneeth R
 */

#include <iostream>
#include <iomanip>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

void printMemoryReport(const MemoryStats& stats) {
    std::cout << "\nMemory Statistics:\n";
    std::cout << "  Atoms: " << stats.atom_count 
              << " (" << stats.tombstone_count << " tombstones)\n";
    std::cout << "  Orphans: " << stats.orphan_count << "\n";
    std::cout << "  Total Memory: " << stats.total_bytes() / 1024 << " KB\n";
    std::cout << "    - Atom List: " << stats.atom_list_bytes / 1024 << " KB\n";
    std::cout << "    - Index Map: " << stats.index_map_bytes / 1024 << " KB\n";
    std::cout << "    - Orphan Buffer: " << stats.orphan_buffer_bytes / 1024 << " KB\n";
    
    if (stats.gc_stats.total_gc_runs > 0) {
        std::cout << "\nGC Performance:\n";
        std::cout << "  Total Runs: " << stats.gc_stats.total_gc_runs << "\n";
        std::cout << "  Tombstones Removed: " << stats.gc_stats.total_tombstones_removed << "\n";
        std::cout << "  Average GC Time: " << std::fixed << std::setprecision(2) 
                  << stats.gc_stats.avg_gc_time_us << " μs\n";
        std::cout << "  Peak GC Time: " << stats.gc_stats.max_gc_time_us << " μs\n";
        std::cout << "  Total GC Time: " << stats.gc_stats.total_gc_time_us / 1000.0 << " ms\n";
    }
}

int main() {
    std::cout << "=== Memory Profiling Example ===\n";
    
    Sequence doc(1);
    
    // Configure auto-GC
    Sequence::GCConfig gc_config;
    gc_config.auto_gc_enabled = true;
    gc_config.tombstone_threshold = 50;
    gc_config.min_age_threshold = 10;
    doc.setGCConfig(gc_config);
    
    std::cout << "\n--- Phase 1: Creating Content ---\n";
    
    // Create initial content
    for (int i = 0; i < 100; i++) {
        doc.localInsert(i, 'A' + (i % 26));
    }
    
    std::cout << "Created 100 atoms\n";
    printMemoryReport(doc.getMemoryStats());
    
    // Create deletions (tombstones)
    std::cout << "\n--- Phase 2: Creating Tombstones ---\n";
    
    for (int i = 0; i < 60; i++) {
        if (!doc.toString().empty()) {
            doc.localDelete(0);
        }
    }
    
    std::cout << "Deleted 60 characters\n";
    printMemoryReport(doc.getMemoryStats());
    
    // Manual GC
    std::cout << "\n--- Phase 3: Manual Garbage Collection ---\n";
    
    size_t removed = doc.garbageCollectLocal(15);
    std::cout << "Manually removed " << removed << " tombstones\n";
    printMemoryReport(doc.getMemoryStats());
    
    // More operations to trigger auto-GC
    std::cout << "\n--- Phase 4: Testing Auto-GC ---\n";
    
    for (int i = 0; i < 30; i++) {
        doc.localInsert(doc.toString().size(), 'X');
    }
    
    for (int i = 0; i < 30; i++) {
        if (!doc.toString().empty()) {
            doc.localDelete(0);
        }
    }
    
    std::cout << "Created and deleted 30 more characters\n";
    std::cout << "Auto-GC should have triggered...\n";
    printMemoryReport(doc.getMemoryStats());
    
    // Final statistics
    std::cout << "\n--- Final Report ---\n";
    MemoryStats final_stats = doc.getMemoryStats();
    
    std::cout << "Final content: \"" << doc.toString() << "\"\n";
    std::cout << "Final atom count: " << final_stats.atom_count << "\n";
    std::cout << "Final tombstone count: " << final_stats.tombstone_count << "\n";
    std::cout << "Total memory: " << final_stats.total_bytes() / 1024 << " KB\n";
    
    if (final_stats.gc_stats.total_gc_runs > 0) {
        double avg_efficiency = (double)final_stats.gc_stats.total_tombstones_removed 
                               / final_stats.gc_stats.total_gc_runs;
        std::cout << "\nGC Efficiency: " << std::fixed << std::setprecision(1) 
                  << avg_efficiency << " tombstones/run\n";
        
        double overhead_percent = (final_stats.gc_stats.total_gc_time_us / 1000000.0) 
                                  / (final_stats.gc_stats.total_gc_runs * 0.001) * 100;
        std::cout << "GC Overhead: " << std::fixed << std::setprecision(4) 
                  << overhead_percent << "%\n";
    }
    
    return 0;
}
