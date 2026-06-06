#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <cassert>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

struct Timer {
    std::chrono::high_resolution_clock::time_point start_time;
    Timer() { start_time = std::chrono::high_resolution_clock::now(); }
    
    double elapsed_ms() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_time).count();
    }
};

void run_sequential_append_benchmark(size_t num_ops) {
    std::cout << "--- Scenario 1: Sequential Append (" << num_ops << " inserts at end) ---" << std::endl;
    Sequence doc(1);
    
    Timer timer;
    for (size_t i = 0; i < num_ops; ++i) {
        doc.localInsert(i, 'A');
    }
    double duration = timer.elapsed_ms();
    
    std::cout << "  Completed in: " << duration << " ms" << std::endl;
    std::cout << "  Throughput:   " << (num_ops / (duration / 1000.0)) << " ops/sec" << std::endl;
    std::cout << "  Avg latency:  " << (duration * 1000.0 / num_ops) << " μs/op" << std::endl << std::endl;
}

void run_random_insert_benchmark(size_t num_ops) {
    std::cout << "--- Scenario 2: Random Inserts (" << num_ops << " inserts at random visual indices) ---" << std::endl;
    Sequence doc(1);
    
    std::mt19937 rng(42);
    
    Timer timer;
    for (size_t i = 0; i < num_ops; ++i) {
        size_t current_len = i;
        std::uniform_int_distribution<size_t> dist(0, current_len);
        size_t index = dist(rng);
        doc.localInsert(index, 'x');
    }
    double duration = timer.elapsed_ms();
    
    std::cout << "  Completed in: " << duration << " ms" << std::endl;
    std::cout << "  Throughput:   " << (num_ops / (duration / 1000.0)) << " ops/sec" << std::endl;
    std::cout << "  Avg latency:  " << (duration * 1000.0 / num_ops) << " μs/op" << std::endl << std::endl;
}

void run_random_delete_benchmark(size_t start_size, size_t num_deletes) {
    std::cout << "--- Scenario 3: Random Deletes (" << num_deletes << " deletes from " << start_size << " document) ---" << std::endl;
    Sequence doc(1);
    for (size_t i = 0; i < start_size; ++i) {
        doc.localInsert(i, 'y');
    }
    
    std::mt19937 rng(1337);
    
    Timer timer;
    for (size_t i = 0; i < num_deletes; ++i) {
        size_t current_len = start_size - i;
        if (current_len == 0) break;
        std::uniform_int_distribution<size_t> dist(0, current_len - 1);
        size_t index = dist(rng);
        doc.localDelete(index);
    }
    double duration = timer.elapsed_ms();
    
    std::cout << "  Completed in: " << duration << " ms" << std::endl;
    std::cout << "  Throughput:   " << (num_deletes / (duration / 1000.0)) << " ops/sec" << std::endl;
    std::cout << "  Avg latency:  " << (duration * 1000.0 / num_deletes) << " μs/op" << std::endl << std::endl;
}

void run_sync_performance_benchmark(size_t doc_size, size_t delta_size) {
    std::cout << "--- Scenario 4: Delta Sync & Merge (" << doc_size << " base doc, " << delta_size << " new edits) ---" << std::endl;
    
    Sequence alice(1);
    Sequence bob(2);
    
    // Populate base document
    for (size_t i = 0; i < doc_size; ++i) {
        alice.localInsert(i, 'a');
    }
    
    // Initial sync Bob -> Alice state
    bob.applyDelta(alice.getDelta(bob.getVectorClock()));
    bob.mergeVectorClock(alice.getVectorClock());
    
    // Alice does delta_size insertions
    for (size_t i = 0; i < delta_size; ++i) {
        alice.localInsert(doc_size + i, 'b');
    }
    
    // Benchmark sync Alice -> Bob
    Timer timer_get;
    std::vector<Atom> delta = alice.getDelta(bob.getVectorClock());
    double duration_get = timer_get.elapsed_ms();
    
    Timer timer_apply;
    bob.applyDelta(delta);
    double duration_apply = timer_apply.elapsed_ms();
    
    std::cout << "  Delta generation: " << duration_get << " ms" << std::endl;
    std::cout << "  Delta size:        " << delta.size() << " atoms" << std::endl;
    std::cout << "  Delta merge:       " << duration_apply << " ms" << std::endl;
    std::cout << "  Total sync time:   " << (duration_get + duration_apply) << " ms" << std::endl;
    std::cout << "  Bob content matches Alice: " << (bob.toString() == alice.toString() ? "YES" : "NO") << std::endl << std::endl;
}

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << "     OmniSync Performance Benchmarks       " << std::endl;
    std::cout << "===========================================" << std::endl << std::endl;
    
    // Run benchmarks with different sizes
    run_sequential_append_benchmark(10000);
    run_sequential_append_benchmark(30000);
    
    run_random_insert_benchmark(5000);
    run_random_insert_benchmark(15000);
    
    run_random_delete_benchmark(10000, 5000);
    run_random_delete_benchmark(30000, 15000);
    
    run_sync_performance_benchmark(10000, 2000);
    
    std::cout << "===========================================" << std::endl;
    std::cout << "           Benchmarks Complete             " << std::endl;
    std::cout << "===========================================" << std::endl;
    
    return 0;
}
