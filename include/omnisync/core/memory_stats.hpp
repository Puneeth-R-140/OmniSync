#ifndef OMNISYNC_CORE_MEMORY_STATS_HPP
#define OMNISYNC_CORE_MEMORY_STATS_HPP

#include <cstddef>
#include <iostream>
#include <map>

namespace omnisync::core {

/**
 * @brief Detailed memory usage statistics with profiling
 */
struct MemoryStats {
    // Basic counts
    size_t atom_count = 0;
    size_t tombstone_count = 0;
    size_t orphan_count = 0;
    size_t delete_buffer_count = 0;
    
    // Memory breakdown
    size_t atom_list_bytes = 0;
    size_t index_map_bytes = 0;
    size_t orphan_buffer_bytes = 0;
    size_t vector_clock_bytes = 0;
    
    // Histograms (atom age distribution)
    std::map<size_t, size_t> atom_age_histogram;      // age_bucket -> count
    std::map<size_t, size_t> tombstone_age_histogram; // age_bucket -> count
    
    // GC performance metrics
    struct GCStats {
        size_t total_gc_runs = 0;
        size_t total_tombstones_removed = 0;
        uint64_t total_gc_time_us = 0;      // Total time in GC (microseconds)
        uint64_t last_gc_time_us = 0;       // Last GC duration
        uint64_t max_gc_time_us = 0;        // Peak GC time
        double avg_gc_time_us = 0.0;        // Average GC time
        
        void recordGCRun(uint64_t duration_us, size_t removed) {
            total_gc_runs++;
            total_tombstones_removed += removed;
            total_gc_time_us += duration_us;
            last_gc_time_us = duration_us;
            
            if (duration_us > max_gc_time_us) {
                max_gc_time_us = duration_us;
            }
            
            avg_gc_time_us = (double)total_gc_time_us / total_gc_runs;
        }
    };
    
    GCStats gc_stats;
    
    /**
     * @brief Calculate total memory usage
     */
    size_t total_bytes() const {
        return atom_list_bytes + index_map_bytes + orphan_buffer_bytes + vector_clock_bytes;
    }
    
    /**
     * @brief Print human-readable statistics
     */
    void print() const {
        std::cout << "Memory Statistics:\n";
        std::cout << "  Atoms: " << atom_count << " (" << tombstone_count << " tombstones)\n";
        std::cout << "  Orphans: " << orphan_count << "\n";
        std::cout << "  Delete Buffer: " << delete_buffer_count << "\n";
        std::cout << "  Total Memory: " << total_bytes() / 1024 << " KB\n";
        std::cout << "    - Atom List: " << atom_list_bytes / 1024 << " KB\n";
        std::cout << "    - Index Map: " << index_map_bytes / 1024 << " KB\n";
        std::cout << "    - Orphan Buffer: " << orphan_buffer_bytes / 1024 << " KB\n";
        std::cout << "    - Vector Clock: " << vector_clock_bytes / 1024 << " KB\n";
        
        if (gc_stats.total_gc_runs > 0) {
            std::cout << "\nGC Performance:\n";
            std::cout << "  Total Runs: " << gc_stats.total_gc_runs << "\n";
            std::cout << "  Tombstones Removed: " << gc_stats.total_tombstones_removed << "\n";
            std::cout << "  Total GC Time: " << gc_stats.total_gc_time_us / 1000.0 << " ms\n";
            std::cout << "  Average GC Time: " << gc_stats.avg_gc_time_us / 1000.0 << " ms\n";
            std::cout << "  Last GC Time: " << gc_stats.last_gc_time_us / 1000.0 << " ms\n";
            std::cout << "  Peak GC Time: " << gc_stats.max_gc_time_us / 1000.0 << " ms\n";
        }
    }
    
    /**
     * @brief Calculate average atom age (in clock ticks)
     */
    double getAverageAtomAge() const {
        if (atom_age_histogram.empty()) return 0.0;
        
        size_t total_age = 0;
        size_t total_count = 0;
        
        for (const auto& [age, count] : atom_age_histogram) {
            total_age += age * count;
            total_count += count;
        }
        
        return total_count > 0 ? (double)total_age / total_count : 0.0;
    }
    
    /**
     * @brief Calculate average tombstone age
     */
    double getAverageTombstoneAge() const {
        if (tombstone_age_histogram.empty()) return 0.0;
        
        size_t total_age = 0;
        size_t total_count = 0;
        
        for (const auto& [age, count] : tombstone_age_histogram) {
            total_age += age * count;
            total_count += count;
        }
        
        return total_count > 0 ? (double)total_age / total_count : 0.0;
    }
};

} // namespace omnisync::core

#endif // OMNISYNC_CORE_MEMORY_STATS_HPP
