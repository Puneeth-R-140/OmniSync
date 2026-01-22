#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

/**
 * Stability Monitor - Tracks memory usage and detects leaks
 */
class StabilityMonitor {
public:
    struct Snapshot {
        size_t timestamp_seconds;
        size_t atom_count;
        size_t tombstone_count;
        size_t orphan_count;
        size_t memory_bytes;
        size_t operations_performed;
    };

private:
    std::vector<Snapshot> history_;
    std::chrono::steady_clock::time_point start_time_;
    size_t total_operations_;

public:
    StabilityMonitor() 
        : start_time_(std::chrono::steady_clock::now())
        , total_operations_(0) {}

    void recordSnapshot(const Sequence& doc) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
        
        MemoryStats stats = doc.getMemoryStats();
        
        Snapshot snap;
        snap.timestamp_seconds = elapsed.count();
        snap.atom_count = stats.atom_count;
        snap.tombstone_count = stats.tombstone_count;
        snap.orphan_count = stats.orphan_count;
        snap.memory_bytes = stats.total_bytes();
        snap.operations_performed = total_operations_;
        
        history_.push_back(snap);
    }

    void incrementOperations() {
        total_operations_++;
    }

    bool detectMemoryLeak() const {
        if (history_.size() < 10) return false;
        
        // Check if memory is trending upward over last 10 snapshots
        size_t start_idx = history_.size() >= 10 ? history_.size() - 10 : 0;
        
        size_t first_memory = history_[start_idx].memory_bytes;
        size_t last_memory = history_.back().memory_bytes;
        
        // If memory increased by more than 50% without proportional atom increase
        double memory_ratio = (double)last_memory / first_memory;
        double atom_ratio = (double)history_.back().atom_count / history_[start_idx].atom_count;
        
        return (memory_ratio > 1.5 && atom_ratio < 1.2);
    }

    void printReport(std::ostream& out) const {
        out << "\n=== STABILITY TEST REPORT ===\n\n";
        
        out << "Duration: " << history_.back().timestamp_seconds << " seconds\n";
        out << "Total Operations: " << total_operations_ << "\n";
        out << "Snapshots Recorded: " << history_.size() << "\n\n";
        
        out << "Memory Trend:\n";
        out << "  Initial: " << history_.front().memory_bytes / 1024 << " KB\n";
        out << "  Final:   " << history_.back().memory_bytes / 1024 << " KB\n";
        out << "  Peak:    " << getMaxMemory() / 1024 << " KB\n\n";
        
        out << "Atom Count:\n";
        out << "  Initial: " << history_.front().atom_count << "\n";
        out << "  Final:   " << history_.back().atom_count << "\n\n";
        
        out << "Tombstone Count:\n";
        out << "  Initial: " << history_.front().tombstone_count << "\n";
        out << "  Final:   " << history_.back().tombstone_count << "\n";
        out << "  Max:     " << getMaxTombstones() << "\n\n";
        
        bool leak = detectMemoryLeak();
        out << "Memory Leak Detected: " << (leak ? "YES " : "NO ") << "\n";
    }

    void exportCSV(const std::string& filename) const {
        std::ofstream file(filename);
        file << "Timestamp,AtomCount,TombstoneCount,OrphanCount,MemoryBytes,Operations\n";
        
        for (const auto& snap : history_) {
            file << snap.timestamp_seconds << ","
                 << snap.atom_count << ","
                 << snap.tombstone_count << ","
                 << snap.orphan_count << ","
                 << snap.memory_bytes << ","
                 << snap.operations_performed << "\n";
        }
    }

private:
    size_t getMaxMemory() const {
        size_t max_mem = 0;
        for (const auto& snap : history_) {
            max_mem = std::max(max_mem, snap.memory_bytes);
        }
        return max_mem;
    }

    size_t getMaxTombstones() const {
        size_t max_tombs = 0;
        for (const auto& snap : history_) {
            max_tombs = std::max(max_tombs, snap.tombstone_count);
        }
        return max_tombs;
    }
};

/**
 * Stability Test Configuration
 */
struct TestConfig {
    size_t duration_hours = 24;
    size_t num_users = 5;
    size_t ops_per_second = 10;
    size_t snapshot_interval_seconds = 300;  // 5 minutes
    size_t gc_interval_seconds = 600;        // 10 minutes
    bool enable_auto_gc = true;
};

/**
 * Parse command line arguments
 */
TestConfig parseArgs(int argc, char* argv[]) {
    TestConfig config;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--duration-hours" && i + 1 < argc) {
            config.duration_hours = std::stoul(argv[++i]);
        } else if (arg == "--users" && i + 1 < argc) {
            config.num_users = std::stoul(argv[++i]);
        } else if (arg == "--ops-per-second" && i + 1 < argc) {
            config.ops_per_second = std::stoul(argv[++i]);
        } else if (arg == "--no-auto-gc") {
            config.enable_auto_gc = false;
        } else if (arg == "--help") {
            std::cout << "Usage: stability_test [options]\n";
            std::cout << "  --duration-hours N    Run for N hours (default: 24)\n";
            std::cout << "  --users N             Simulate N users (default: 5)\n";
            std::cout << "  --ops-per-second N    Operations per second (default: 10)\n";
            std::cout << "  --no-auto-gc          Disable automatic GC\n";
            exit(0);
        }
    }
    
    return config;
}

/**
 * Main stability test
 */
int main(int argc, char* argv[]) {
    TestConfig config = parseArgs(argc, argv);
    
    std::cout << "=== OmniSync Stability Test ===\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  Duration: " << config.duration_hours << " hours\n";
    std::cout << "  Users: " << config.num_users << "\n";
    std::cout << "  Operations/second: " << config.ops_per_second << "\n";
    std::cout << "  Snapshot interval: " << config.snapshot_interval_seconds << "s\n";
    std::cout << "  GC interval: " << config.gc_interval_seconds << "s\n";
    std::cout << "  Auto-GC: " << (config.enable_auto_gc ? "Enabled" : "Disabled") << "\n\n";
    
    // Initialize users (use pointers since Sequence is non-copyable)
    std::vector<std::unique_ptr<Sequence>> users;
    for (size_t i = 0; i < config.num_users; i++) {
        users.push_back(std::make_unique<Sequence>(i + 1));
        
        if (config.enable_auto_gc) {
            Sequence::GCConfig gc_config;
            gc_config.auto_gc_enabled = true;
            gc_config.tombstone_threshold = 1000;
            gc_config.min_age_threshold = 100;
            users[i]->setGCConfig(gc_config);
        }
    }
    
    // Initialize monitor
    StabilityMonitor monitor;
    
    // Random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> user_dist(0, config.num_users - 1);
    std::uniform_int_distribution<> op_dist(0, 1);  // 0=insert, 1=delete
    
    // Timing
    auto test_start = std::chrono::steady_clock::now();
    auto duration = std::chrono::hours(config.duration_hours);
    auto last_snapshot = test_start;
    auto last_gc = test_start;
    auto last_progress = test_start;
    
    size_t checkpoint_count = 0;
    size_t gc_count = 0;
    
    std::cout << "Starting test... (Press Ctrl+C to stop early)\n\n";
    
    monitor.recordSnapshot(*users[0]);  // Initial snapshot
    
    // Main test loop
    while (std::chrono::steady_clock::now() - test_start < duration) {
        auto now = std::chrono::steady_clock::now();
        
        // Perform random operations
        for (size_t i = 0; i < config.ops_per_second; i++) {
            size_t user_idx = user_dist(gen);
            Sequence& user = *users[user_idx];
            
            if (op_dist(gen) == 0 || user.toString().empty()) {
                // Insert
                size_t pos = user.toString().length();
                char ch = 'A' + (rand() % 26);
                Atom atom = user.localInsert(pos, ch);
                
                // Broadcast to other users
                for (size_t j = 0; j < users.size(); j++) {
                    if (j != user_idx) {
                        users[j]->remoteMerge(atom);
                    }
                }
            } else {
                // Delete
                size_t len = user.toString().length();
                if (len > 0) {
                    size_t pos = rand() % len;
                    OpID deleted = user.localDelete(pos);
                    
                    // Broadcast
                    for (size_t j = 0; j < users.size(); j++) {
                        if (j != user_idx) {
                            users[j]->remoteDelete(deleted);
                        }
                    }
                }
            }
            
            monitor.incrementOperations();
        }
        
        // Snapshot
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_snapshot).count() 
            >= config.snapshot_interval_seconds) {
            monitor.recordSnapshot(*users[0]);
            checkpoint_count++;
            last_snapshot = now;
        }
        
        // Manual GC (if auto-GC is disabled)
        if (!config.enable_auto_gc && 
            std::chrono::duration_cast<std::chrono::seconds>(now - last_gc).count() 
            >= config.gc_interval_seconds) {
            for (auto& user : users) {
                user->garbageCollectLocal(100);
            }
            gc_count++;
            last_gc = now;
        }
        
        // Progress report every minute
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_progress).count() >= 60) {
            auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - test_start);
            std::cout << "Progress: " << elapsed.count() << "h / " << config.duration_hours << "h "
                     << "(Memory: " << users[0]->getMemoryStats().total_bytes() / 1024 << " KB, "
                     << "Tombstones: " << users[0]->getTombstoneCount() << ")\n";
            last_progress = now;
        }
        
        // Sleep to maintain ops/second rate
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    std::cout << "\n\nTest complete. Verifying convergence...\n";
    
    // Final snapshot
    monitor.recordSnapshot(*users[0]);
    
    // Verify all users converged
    std::string reference = users[0]->toString();
    bool all_converged = true;
    
    for (size_t i = 1; i < users.size(); i++) {
        if (users[i]->toString() != reference) {
            std::cout << "CONVERGENCE FAILURE: User " << i << " differs\n";
            all_converged = false;
        }
    }
    
    if (all_converged) {
        std::cout << "SUCCESS: All " << users.size() << " users converged\n";
    }
    
    // Print report
    monitor.printReport(std::cout);
    
    // Export CSV
    monitor.exportCSV("stability_test_results.csv");
    std::cout << "\nDetailed results exported to: stability_test_results.csv\n";
    
    return all_converged && !monitor.detectMemoryLeak() ? 0 : 1;
}
