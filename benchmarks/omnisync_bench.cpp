#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <cstdlib>
#include "../include/omnisync/core/sequence.hpp"

using namespace omnisync::core;
using Clock = std::chrono::high_resolution_clock;
using Ms    = std::chrono::duration<double, std::milli>;

// ── helpers ───────────────────────────────────────────────────────────────────
static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2 == 0) ? (v[n/2-1] + v[n/2]) / 2.0 : v[n/2];
}

template<typename F>
double time_ms(F&& fn) {
    auto t0 = Clock::now();
    fn();
    return Ms(Clock::now() - t0).count();
}

static const int N       = 10000;   // ops per workload
static const int REPEATS = 5;       // runs per scenario
static const unsigned SEED = 42;

// ── Workload 1: Sequential append ─────────────────────────────────────────────
double bench_sequential_insert() {
    std::vector<double> times;
    for (int r = 0; r < REPEATS; r++) {
        times.push_back(time_ms([&] {
            Sequence doc(1);
            for (int i = 0; i < N; i++)
                doc.localInsert(i, 'a' + (i % 26));
        }));
    }
    return median(times);
}

// ── Workload 2: Random position insert ────────────────────────────────────────
double bench_random_insert() {
    // Pre-generate positions to exclude from timing
    srand(SEED);
    std::vector<size_t> positions;
    for (int i = 0; i < N; i++)
        positions.push_back(i == 0 ? 0 : rand() % (i + 1));

    std::vector<double> times;
    for (int r = 0; r < REPEATS; r++) {
        times.push_back(time_ms([&] {
            Sequence doc(1);
            for (int i = 0; i < N; i++)
                doc.localInsert(positions[i], 'a' + (i % 26));
        }));
    }
    return median(times);
}

// ── Workload 3: Sequential delete from front ──────────────────────────────────
double bench_sequential_delete() {
    // Pre-build atoms so we can replay them cheaply
    std::vector<Atom> build_ops;
    {
        Sequence proto(1);
        for (int i = 0; i < N; i++)
            build_ops.push_back(proto.localInsert(i, 'a' + (i % 26)));
    }

    std::vector<double> times;
    for (int r = 0; r < REPEATS; r++) {
        // Rebuild doc by replaying ops (much cheaper than copy, avoids the issue)
        Sequence doc(1);
        for (auto& op : build_ops) doc.remoteMerge(op);

        times.push_back(time_ms([&] {
            for (int i = 0; i < N; i++)
                doc.localDelete(0);
        }));
    }
    return median(times);
}

// ── Workload 4: Concurrent merge (two peers, N/2 ops each) ────────────────────
double bench_concurrent_merge() {
    srand(SEED);
    // Generate ops offline
    Sequence alice(1), bob(2);
    std::vector<Atom> alice_ops, bob_ops;
    for (int i = 0; i < N/2; i++)
        alice_ops.push_back(alice.localInsert(i, 'a' + (i % 26)));
    for (int i = 0; i < N/2; i++)
        bob_ops.push_back(bob.localInsert(i, 'A' + (i % 26)));

    std::vector<double> times;
    for (int r = 0; r < REPEATS; r++) {
        Sequence a2(1), b2(2);
        for (auto& op : alice_ops) a2.remoteMerge(op);
        for (auto& op : bob_ops)   b2.remoteMerge(op);

        times.push_back(time_ms([&] {
            // Cross-sync: alice gets bob's ops, bob gets alice's ops
            for (auto& op : bob_ops)   a2.remoteMerge(op);
            for (auto& op : alice_ops) b2.remoteMerge(op);
        }));
    }
    return median(times);
}

// ── Workload 5: toString / snapshot ───────────────────────────────────────────
double bench_to_string() {
    Sequence doc(1);
    for (int i = 0; i < N; i++) doc.localInsert(i, 'a' + (i % 26));

    const int SNAP_REPS = 1000;
    std::vector<double> times;
    for (int r = 0; r < REPEATS; r++) {
        times.push_back(time_ms([&] {
            volatile size_t sink = 0;
            for (int i = 0; i < SNAP_REPS; i++)
                sink += doc.toString().size();
        }));
    }
    return median(times);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "\n[OmniSync C++ Benchmark] N=" << N << " ops, "
              << REPEATS << " runs, median reported\n\n";

    auto r1 = bench_sequential_insert();
    auto r2 = bench_random_insert();
    auto r3 = bench_sequential_delete();
    auto r4 = bench_concurrent_merge();
    auto r5 = bench_to_string();

    // Print as CSV so the PowerShell runner can parse it
    std::cout << "RESULT:sequential_insert:"   << r1 << "\n";
    std::cout << "RESULT:random_insert:"        << r2 << "\n";
    std::cout << "RESULT:sequential_delete:"    << r3 << "\n";
    std::cout << "RESULT:concurrent_merge:"     << r4 << "\n";
    std::cout << "RESULT:tostring_snapshot:"    << r5 << "\n";

    return 0;
}
