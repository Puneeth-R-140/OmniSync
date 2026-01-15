#pragma once

#include <cstdint>
#include <iostream>

namespace omnisync {
namespace core {

/**
 * @brief Memory usage statistics for a Sequence.
 * 
 * Provides detailed breakdown of memory consumption across
 * different data structures. Useful for profiling and debugging
 * memory issues in long-running sessions.
 */
struct MemoryStats {
    size_t atom_count;
    size_t tombstone_count;
    size_t orphan_count;
    size_t delete_buffer_count;
    
    size_t atom_list_bytes;
    size_t index_map_bytes;
    size_t orphan_buffer_bytes;
    size_t vector_clock_bytes;
    
    /**
     * @brief Calculate total memory usage in bytes.
     */
    size_t total_bytes() const {
        return atom_list_bytes + index_map_bytes + 
               orphan_buffer_bytes + vector_clock_bytes;
    }
    
    /**
     * @brief Print statistics to stdout.
     */
    void print() const {
        std::cout << "Memory Statistics:" << std::endl;
        std::cout << "  Atoms: " << atom_count 
                  << " (" << tombstone_count << " tombstones)" << std::endl;
        std::cout << "  Orphans: " << orphan_count << std::endl;
        std::cout << "  Delete Buffer: " << delete_buffer_count << std::endl;
        std::cout << "  Total Memory: " << total_bytes() / 1024 << " KB" << std::endl;
        std::cout << "    - Atom List: " << atom_list_bytes / 1024 << " KB" << std::endl;
        std::cout << "    - Index Map: " << index_map_bytes / 1024 << " KB" << std::endl;
        std::cout << "    - Orphan Buffer: " << orphan_buffer_bytes / 1024 << " KB" << std::endl;
        std::cout << "    - Vector Clock: " << vector_clock_bytes / 1024 << " KB" << std::endl;
    }
};

} // namespace core
} // namespace omnisync
