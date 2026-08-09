#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <ostream>

namespace omnisync::core {

/** Lightweight, explicitly approximate memory and GC telemetry. */
struct MemoryStats {
    std::size_t atom_count = 0;
    std::size_t tombstone_count = 0;
    std::size_t orphan_count = 0;
    std::size_t delete_buffer_count = 0;

    std::size_t atom_list_bytes = 0;
    std::size_t index_map_bytes = 0;
    std::size_t orphan_buffer_bytes = 0;
    std::size_t delete_buffer_bytes = 0;
    std::size_t mark_bytes = 0;
    std::size_t vector_clock_bytes = 0;

    std::map<std::size_t, std::size_t> atom_age_histogram;
    std::map<std::size_t, std::size_t> tombstone_age_histogram;

    struct GCStats {
        std::size_t total_gc_runs = 0;
        std::size_t total_tombstones_removed = 0;
        std::uint64_t total_gc_time_us = 0;
        std::uint64_t last_gc_time_us = 0;
        std::uint64_t max_gc_time_us = 0;
        double avg_gc_time_us = 0.0;

        void recordGCRun(std::uint64_t duration_us, std::size_t removed) noexcept {
            if (total_gc_runs != std::numeric_limits<std::size_t>::max()) {
                ++total_gc_runs;
            }
            if (std::numeric_limits<std::size_t>::max() - total_tombstones_removed < removed) {
                total_tombstones_removed = std::numeric_limits<std::size_t>::max();
            } else {
                total_tombstones_removed += removed;
            }
            if (std::numeric_limits<std::uint64_t>::max() - total_gc_time_us < duration_us) {
                total_gc_time_us = std::numeric_limits<std::uint64_t>::max();
            } else {
                total_gc_time_us += duration_us;
            }
            last_gc_time_us = duration_us;
            if (duration_us > max_gc_time_us) max_gc_time_us = duration_us;
            if (total_gc_runs != 0) {
                avg_gc_time_us = static_cast<double>(total_gc_time_us) /
                                 static_cast<double>(total_gc_runs);
            }
        }
    };

    GCStats gc_stats;

    std::size_t total_bytes() const noexcept {
        std::size_t total = 0;
        total = saturatingAdd(total, atom_list_bytes);
        total = saturatingAdd(total, index_map_bytes);
        total = saturatingAdd(total, orphan_buffer_bytes);
        total = saturatingAdd(total, delete_buffer_bytes);
        total = saturatingAdd(total, mark_bytes);
        total = saturatingAdd(total, vector_clock_bytes);
        return total;
    }

    void print(std::ostream& out = std::cout) const {
        out << "Memory Statistics:\n"
            << "  Atoms: " << atom_count << " (" << tombstone_count << " tombstones)\n"
            << "  Orphans: " << orphan_count << "\n"
            << "  Delete Buffer Entries: " << delete_buffer_count << "\n"
            << "  Approx. Total Memory: " << total_bytes() / 1024.0 << " KB\n"
            << "    - Atom List: " << atom_list_bytes / 1024.0 << " KB\n"
            << "    - Index: " << index_map_bytes / 1024.0 << " KB\n"
            << "    - Orphan Buffer: " << orphan_buffer_bytes / 1024.0 << " KB\n"
            << "    - Delete Buffer: " << delete_buffer_bytes / 1024.0 << " KB\n"
            << "    - Marks: " << mark_bytes / 1024.0 << " KB\n"
            << "    - Vector Clock: " << vector_clock_bytes / 1024.0 << " KB\n";

        if (gc_stats.total_gc_runs != 0) {
            out << "\nGC Performance:\n"
                << "  Total Runs: " << gc_stats.total_gc_runs << "\n"
                << "  Tombstones Removed: " << gc_stats.total_tombstones_removed << "\n"
                << "  Total GC Time: " << gc_stats.total_gc_time_us / 1000.0 << " ms\n"
                << "  Average GC Time: " << gc_stats.avg_gc_time_us / 1000.0 << " ms\n"
                << "  Last GC Time: " << gc_stats.last_gc_time_us / 1000.0 << " ms\n"
                << "  Peak GC Time: " << gc_stats.max_gc_time_us / 1000.0 << " ms\n";
        }
    }

    double getAverageAtomAge() const noexcept { return weightedAverage(atom_age_histogram); }
    double getAverageTombstoneAge() const noexcept {
        return weightedAverage(tombstone_age_histogram);
    }

private:
    static std::size_t saturatingAdd(std::size_t a, std::size_t b) noexcept {
        if (std::numeric_limits<std::size_t>::max() - a < b) {
            return std::numeric_limits<std::size_t>::max();
        }
        return a + b;
    }

    static double weightedAverage(const std::map<std::size_t, std::size_t>& histogram) noexcept {
        long double age_sum = 0;
        std::size_t count = 0;
        for (const auto& [age, n] : histogram) {
            age_sum += static_cast<long double>(age) * static_cast<long double>(n);
            count += n;
        }
        return count == 0 ? 0.0 : static_cast<double>(age_sum / count);
    }
};

} // namespace omnisync::core
