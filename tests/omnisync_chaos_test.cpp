#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <cassert>
#include <memory>
#include <unordered_map>

#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

// ============================================================
// OmniSync Chaos / Adversarial Test
//
// Purpose:
//   Exercise convergence, idempotency, reordering, deletes,
//   pending deletes, orphan delivery, delta sync, and GC.
//
// Every randomized failure prints the seed so it can be
// reproduced.
// ============================================================

static constexpr unsigned int DEFAULT_SEED = 1337;
static constexpr int DEFAULT_USERS = 5;
static constexpr int DEFAULT_ROUNDS = 1000;

struct DeletePacket {
    OpID target;
    OpID delete_operation;
};

struct Packet {
    int from;
    bool is_delete;
    Atom atom;
    DeletePacket deletion;
};

static bool same_document(
    const std::vector<std::shared_ptr<Sequence>>& users
) {
    if (users.empty()) return true;

    const std::string expected = users[0]->toString();

    for (size_t i = 1; i < users.size(); ++i) {
        if (users[i]->toString() != expected) {
            return false;
        }
    }

    return true;
}

static void print_documents(
    const std::vector<std::shared_ptr<Sequence>>& users
) {
    for (size_t i = 0; i < users.size(); ++i) {
        std::cout << "  User " << (i + 1)
                  << ": \"" << users[i]->toString()
                  << "\"\n";
    }
}

static OpID get_delete_operation(
    Sequence& seq,
    OpID target
) {
    const auto& ops = seq.getDeleteOperationIds(target);

    assert(!ops.empty());

    return ops.back();
}

// ============================================================
// TEST 1
// Basic insert convergence
// ============================================================

static void test_basic_insert() {
    std::cout << "\n[1] Basic insert convergence...\n";

    Sequence a(1);
    Sequence b(2);
    Sequence c(3);

    std::vector<Atom> ops;

    for (int i = 0; i < 100; ++i) {
        ops.push_back(
            a.localInsert(i, static_cast<char>('A' + (i % 26)))
        );
    }

    for (const auto& op : ops) {
        b.remoteMerge(op);
        c.remoteMerge(op);
    }

    assert(a.toString() == b.toString());
    assert(b.toString() == c.toString());

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 2
// Duplicate delivery
// ============================================================

static void test_duplicate_delivery() {
    std::cout << "\n[2] Duplicate delivery...\n";

    Sequence a(1);
    Sequence b(2);

    std::vector<Atom> ops;

    for (int i = 0; i < 50; ++i) {
        ops.push_back(a.localInsert(i, 'X'));
    }

    // Deliver every insertion multiple times.
    for (const auto& op : ops) {
        b.remoteMerge(op);
        b.remoteMerge(op);
        b.remoteMerge(op);
    }

    assert(a.toString() == b.toString());

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 3
// Delete after synchronization
// ============================================================

static void test_delete_after_sync() {
    std::cout << "\n[3] Delete after synchronization...\n";

    Sequence a(1);
    Sequence b(2);

    std::vector<Atom> ops;

    for (int i = 0; i < 20; ++i) {
    ops.push_back(
        a.localInsert(i, static_cast<char>('A' + i))
    );
}

    for (const auto& op : ops) {
        b.remoteMerge(op);
    }

    if (a.toString() != b.toString()) {
    std::cout << "  FAILURE after delete\n";
    std::cout << "  A: \"" << a.toString() << "\"\n";
    std::cout << "  B: \"" << b.toString() << "\"\n";
    std::cout << "  A tombstones: " << a.getTombstoneCount() << "\n";
    std::cout << "  B tombstones: " << b.getTombstoneCount() << "\n";
    assert(false);
}

    for (int i = 0; i < 10; ++i) {
    OpID target = a.localDelete(0);

    std::cout << "  Delete " << i
              << ": target=" << target.client_id
              << ":" << target.clock
              << " A=\"" << a.toString() << "\"\n";

    if (target.clock == 0) {
        std::cout << "  ERROR: localDelete returned invalid target\n";
        assert(false);
    }

    const auto& delete_ops = a.getDeleteOperationIds(target);

    std::cout << "    delete_ops=" << delete_ops.size();

    if (!delete_ops.empty()) {
        std::cout << " [" 
                  << delete_ops.back().client_id
                  << ":" << delete_ops.back().clock
                  << "]";
    }

    std::cout << "\n";

    assert(!delete_ops.empty());

    b.remoteDelete(target, delete_ops.back());

    std::cout << "    B=\"" << b.toString()
              << "\" tombstones=" << b.getTombstoneCount()
              << "\n";
}
}

// ============================================================
// TEST 4
// Delete-before-insert
// ============================================================

static void test_delete_before_insert() {
    std::cout << "\n[4] Delete-before-insert...\n";

    Sequence a(1);
    Sequence b(2);

    Atom inserted = a.localInsert(0, 'Z');

    OpID target = a.localDelete(0);
    OpID delete_op = get_delete_operation(a, target);

    // Delete arrives first.
    b.remoteDelete(target, delete_op);

    // Then insertion arrives.
    b.remoteMerge(inserted);

    assert(a.toString().empty());
    assert(b.toString().empty());

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 5
// Repeated deletion delivery
// ============================================================

static void test_repeated_delete_delivery() {
    std::cout << "\n[5] Repeated deletion delivery...\n";

    Sequence a(1);
    Sequence b(2);

    Atom inserted = a.localInsert(0, 'A');

    b.remoteMerge(inserted);

    OpID target = a.localDelete(0);
    OpID delete_op = get_delete_operation(a, target);

    for (int i = 0; i < 100; ++i) {
        b.remoteDelete(target, delete_op);
    }

    assert(a.toString().empty());
    assert(b.toString().empty());
    assert(b.getTombstoneCount() == 1);

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 6
// Delta deletion
// ============================================================

static void test_delta_deletion() {
    std::cout << "\n[6] Deletion delta propagation...\n";

    Sequence a(1);
    Sequence b(2);

    for (int i = 0; i < 30; ++i) {
        Atom op = a.localInsert(i, 'A');
        b.remoteMerge(op);
    }

    VectorClock before_delete = b.getVectorClock();

    OpID target = a.localDelete(10);

    std::vector<Atom> delta = a.getDelta(before_delete);

    assert(!delta.empty());

    b.applyDelta(delta);

    assert(a.toString() == b.toString());

    std::cout << "  Delta size: " << delta.size() << "\n";
    std::cout << "  PASS\n";
}

// ============================================================
// TEST 7
// Out-of-order insertion delivery
// ============================================================

static void test_out_of_order_insertions() {
    std::cout << "\n[7] Out-of-order insertion delivery...\n";

    Sequence a(1);
    Sequence b(2);

    std::vector<Atom> ops;

    for (int i = 0; i < 100; ++i) {
        ops.push_back(
            a.localInsert(i, static_cast<char>('A' + i % 26))
        );
    }

    std::reverse(ops.begin(), ops.end());

    for (const auto& op : ops) {
        b.remoteMerge(op);
    }

    assert(a.toString() == b.toString());

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 8
// Random packet ordering
// ============================================================

static void test_random_delivery(unsigned int seed) {
    std::cout << "\n[8] Random packet delivery, seed="
              << seed << "...\n";

    std::mt19937 rng(seed);

    constexpr int USERS = 5;
    constexpr int OPS = 500;

    std::vector<std::shared_ptr<Sequence>> users;

    for (int i = 0; i < USERS; ++i) {
        users.push_back(
            std::make_shared<Sequence>(i + 1)
        );

        users.back()->setOrphanConfig({
            1000000,
            UINT64_MAX
        });
    }

    std::vector<Packet> packets;

    for (int round = 0; round < OPS; ++round) {
        for (int u = 0; u < USERS; ++u) {
            auto& seq = *users[u];

            std::string current = seq.toString();

            std::uniform_int_distribution<int> type_dist(0, 99);

            bool do_delete =
                !current.empty() &&
                type_dist(rng) < 30;

            if (do_delete) {
                std::uniform_int_distribution<size_t> index_dist(
                    0,
                    current.size() - 1
                );

                size_t index = index_dist(rng);

                OpID target = seq.localDelete(index);

                if (target.clock != 0) {
                    OpID delete_op =
                        get_delete_operation(seq, target);

                    Packet packet{};
                    packet.from = u + 1;
                    packet.is_delete = true;
                    packet.deletion.target = target;
                    packet.deletion.delete_operation = delete_op;

                    packets.push_back(packet);
                }
            } else {
                std::uniform_int_distribution<size_t> index_dist(
                    0,
                    current.size()
                );

                std::uniform_int_distribution<int> char_dist(
                    'A',
                    'Z'
                );

                size_t index = index_dist(rng);
                char value =
                    static_cast<char>(char_dist(rng));

                Atom atom =
                    seq.localInsert(index, value);

                Packet packet{};
                packet.from = u + 1;
                packet.is_delete = false;
                packet.atom = atom;

                packets.push_back(packet);
            }
        }
    }

    std::shuffle(
        packets.begin(),
        packets.end(),
        rng
    );

    for (int u = 0; u < USERS; ++u) {
        auto& seq = *users[u];

        for (const auto& packet : packets) {
            if (packet.from == u + 1) {
                continue;
            }

            if (packet.is_delete) {
                seq.remoteDelete(
                    packet.deletion.target,
                    packet.deletion.delete_operation
                );
            } else {
                seq.remoteMerge(packet.atom);
            }
        }
    }

    if (!same_document(users)) {
        std::cout << "  FAILURE! seed=" << seed << "\n";
        print_documents(users);
        assert(false);
    }

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 9
// Random seeds
// ============================================================

static void test_many_random_seeds() {
    std::cout << "\n[9] Multi-seed randomized convergence...\n";

    const unsigned int seeds[] = {
        1,
        7,
        42,
        1337,
        2024,
        9999,
        123456,
        0xDEADBEEF
    };

    for (unsigned int seed : seeds) {
        test_random_delivery(seed);
    }

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 10
// GC safety
// ============================================================

static void test_gc_safety() {
    std::cout << "\n[10] GC safety...\n";

    Sequence a(1);
    Sequence b(2);

    Atom inserted = a.localInsert(0, 'A');

    OpID target = a.localDelete(0);
    OpID delete_op = get_delete_operation(a, target);

    // B has seen nothing.
    VectorClock empty(2);

    size_t removed =
        a.garbageCollect(empty);

    assert(removed == 0);
    assert(a.getTombstoneCount() == 1);

    // Now deliver insertion + deletion.
    b.remoteMerge(inserted);
    b.remoteDelete(target, delete_op);

    std::vector<VectorClock> states = {
        a.getVectorClock(),
        b.getVectorClock()
    };

    VectorClock frontier =
        VectorClock::computeMinimum(states);

    removed = a.garbageCollect(frontier);

    assert(removed == 1);
    assert(a.getTombstoneCount() == 0);

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 11
// GC convergence
// ============================================================

static void test_gc_convergence() {
    std::cout << "\n[11] GC convergence...\n";

    Sequence a(1);
    Sequence b(2);
    Sequence c(3);

    std::vector<Atom> ops;

    for (int i = 0; i < 50; ++i) {
        ops.push_back(a.localInsert(i, 'X'));
    }

    for (const auto& op : ops) {
        b.remoteMerge(op);
        c.remoteMerge(op);
    }

    for (int i = 0; i < 20; ++i) {
        OpID target = a.localDelete(0);
        OpID delete_op = get_delete_operation(a, target);

        b.remoteDelete(target, delete_op);
        c.remoteDelete(target, delete_op);
    }

    assert(a.toString() == b.toString());
    assert(b.toString() == c.toString());

    std::vector<VectorClock> states = {
        a.getVectorClock(),
        b.getVectorClock(),
        c.getVectorClock()
    };

    VectorClock frontier =
        VectorClock::computeMinimum(states);

    size_t ra = a.garbageCollect(frontier);
    size_t rb = b.garbageCollect(frontier);
    size_t rc = c.garbageCollect(frontier);

    assert(ra == rb);
    assert(rb == rc);

    assert(a.toString() == b.toString());
    assert(b.toString() == c.toString());

    std::cout << "  Removed: " << ra << "\n";
    std::cout << "  PASS\n";
}

// ============================================================
// TEST 12
// Repeated synchronization
// ============================================================

static void test_repeated_sync() {
    std::cout << "\n[12] Repeated synchronization...\n";

    Sequence a(1);
    Sequence b(2);

    for (int i = 0; i < 100; ++i) {
        Atom op = a.localInsert(i, 'A' + (i % 26));
        b.remoteMerge(op);
    }

    for (int round = 0; round < 20; ++round) {
        VectorClock a_state = a.getVectorClock();
        VectorClock b_state = b.getVectorClock();

        std::vector<Atom> a_delta =
            a.getDelta(b_state);

        std::vector<Atom> b_delta =
            b.getDelta(a_state);

        b.applyDelta(a_delta);
        a.applyDelta(b_delta);

        assert(a.toString() == b.toString());
    }

    std::cout << "  PASS\n";
}

// ============================================================
// TEST 13
// Random operations + repeated synchronization
// ============================================================

static void test_random_rounds(unsigned int seed) {
    std::cout << "\n[13] Random operations with repeated sync, seed="
              << seed << "...\n";

    std::mt19937 rng(seed);

    Sequence a(1);
    Sequence b(2);
    Sequence c(3);

    for (int round = 0; round < 1000; ++round) {
        Sequence* seqs[] = {
            &a,
            &b,
            &c
        };

        int owner =
            std::uniform_int_distribution<int>(0, 2)(rng);

        Sequence& seq = *seqs[owner];

        std::string current = seq.toString();

        bool do_delete =
            !current.empty() &&
            std::uniform_int_distribution<int>(0, 99)(rng) < 35;

        if (do_delete) {
            size_t index =
                std::uniform_int_distribution<size_t>(
                    0,
                    current.size() - 1
                )(rng);

            seq.localDelete(index);
        } else {
            size_t index =
                std::uniform_int_distribution<size_t>(
                    0,
                    current.size()
                )(rng);

            char value =
                static_cast<char>(
                    std::uniform_int_distribution<int>(
                        'A',
                        'Z'
                    )(rng)
                );

            seq.localInsert(index, value);
        }

        // Periodic full delta exchange.
        if (round % 10 == 0) {
            VectorClock a_state = a.getVectorClock();
            VectorClock b_state = b.getVectorClock();
            VectorClock c_state = c.getVectorClock();

            a.applyDelta(b.getDelta(a_state));
            a.applyDelta(c.getDelta(a_state));

            b.applyDelta(a.getDelta(b_state));
            b.applyDelta(c.getDelta(b_state));

            c.applyDelta(a.getDelta(c_state));
            c.applyDelta(b.getDelta(c_state));
        }
    }

    // Final convergence pass.
    for (int round = 0; round < 10; ++round) {
        VectorClock a_state = a.getVectorClock();
        VectorClock b_state = b.getVectorClock();
        VectorClock c_state = c.getVectorClock();

        a.applyDelta(b.getDelta(a_state));
        a.applyDelta(c.getDelta(a_state));

        b.applyDelta(a.getDelta(b_state));
        b.applyDelta(c.getDelta(b_state));

        c.applyDelta(a.getDelta(c_state));
        c.applyDelta(b.getDelta(c_state));
    }

    if (!(a.toString() == b.toString() &&
          b.toString() == c.toString())) {

        std::cout << "  FAILURE seed=" << seed << "\n";
std::cout << "  User 1: " << a.toString() << "\n";
std::cout << "  User 2: " << b.toString() << "\n";
std::cout << "  User 3: " << c.toString() << "\n";

assert(false);

        assert(false);
    }

    std::cout << "  PASS\n";
}

// ============================================================
// MAIN
// ============================================================

int main() {
    std::cout << "========================================\n";
    std::cout << " OmniSync Comprehensive Chaos Test\n";
    std::cout << "========================================\n";

    test_basic_insert();
    test_duplicate_delivery();
    test_delete_after_sync();
    test_delete_before_insert();
    test_repeated_delete_delivery();
    test_delta_deletion();
    test_out_of_order_insertions();

    test_many_random_seeds();

    test_gc_safety();
    test_gc_convergence();
    test_repeated_sync();

    test_random_rounds(1337);
    test_random_rounds(42);
    test_random_rounds(2026);

    std::cout << "\n========================================\n";
    std::cout << " ALL CHAOS TESTS PASSED\n";
    std::cout << "========================================\n";

    return 0;
}