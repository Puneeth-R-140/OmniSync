#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdlib>
#include "../include/omnisync/core/sequence.hpp"

using namespace omnisync::core;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Concurrent single-char insertions at same position
// ─────────────────────────────────────────────────────────────────────────────
void test_concurrent_insertions_same_position() {
    std::cout << "[1] Concurrent insertions at same position..." << std::endl;

    Sequence alice(1), bob(2);

    Atom a1 = alice.localInsert(0, 'H');
    Atom a2 = alice.localInsert(1, 'e');
    Atom a3 = alice.localInsert(2, 'l');
    Atom a4 = alice.localInsert(3, 'l');
    Atom a5 = alice.localInsert(4, 'o');

    bob.remoteMerge(a1); bob.remoteMerge(a2); bob.remoteMerge(a3);
    bob.remoteMerge(a4); bob.remoteMerge(a5);

    assert(alice.toString() == "Hello");
    assert(bob.toString()   == "Hello");

    Atom op_alice = alice.localInsert(5, '!');
    Atom op_bob   = bob.localInsert(5, '?');

    alice.remoteMerge(op_bob);
    bob.remoteMerge(op_alice);

    std::string at = alice.toString(), bt = bob.toString();
    std::cout << "    Alice: " << at << "\n    Bob  : " << bt << std::endl;
    assert(at == bt && "CONVERGENCE FAILURE: concurrent insertions at same pos");
    assert((at == "Hello?!" || at == "Hello!?") && "Unexpected interleaving");
    std::cout << "    PASSED\n" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Both users type the SAME WORD simultaneously at the same position
// ─────────────────────────────────────────────────────────────────────────────
void test_both_type_same_word_simultaneously() {
    std::cout << "[2] Both users type same word 'HI' simultaneously at pos 0..." << std::endl;

    Sequence alice(1), bob(2);

    // Both start empty. Alice types 'H','I'. Bob types 'H','I'.
    // All ops are generated offline (no sync yet) — maximum concurrency.
    Atom aH = alice.localInsert(0, 'H');
    Atom aI = alice.localInsert(1, 'I');

    Atom bH = bob.localInsert(0, 'H');
    Atom bI = bob.localInsert(1, 'I');

    // Full cross-sync
    alice.remoteMerge(bH); alice.remoteMerge(bI);
    bob.remoteMerge(aH);   bob.remoteMerge(aI);

    std::string at = alice.toString(), bt = bob.toString();
    std::cout << "    Alice: " << at << "\n    Bob  : " << bt << std::endl;
    assert(at == bt && "CONVERGENCE FAILURE: both type same word simultaneously");
    assert(at.size() == 4 && "Should have 4 chars total (HIHI or HIHI order)");
    std::cout << "    PASSED\n" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Concurrent deletion of the same character — double-delete safety
// ─────────────────────────────────────────────────────────────────────────────
void test_concurrent_deletions() {
    std::cout << "[3] Concurrent deletions of the same character..." << std::endl;

    Sequence alice(1), bob(2);

    Atom a1 = alice.localInsert(0, 'X');
    bob.remoteMerge(a1);

    assert(alice.toString() == "X");
    assert(bob.toString()   == "X");

    OpID del_alice = alice.localDelete(0);
    OpID del_bob   = bob.localDelete(0);

    alice.remoteDelete(del_bob);
    bob.remoteDelete(del_alice);

    assert(alice.toString().empty());
    assert(bob.toString().empty());
    assert(alice.toString() == bob.toString());
    std::cout << "    PASSED\n" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: One user deletes while other inserts at same position
// ─────────────────────────────────────────────────────────────────────────────
void test_delete_vs_insert_at_same_position() {
    std::cout << "[4] Delete vs insert at the same position concurrently..." << std::endl;

    Sequence alice(1), bob(2);

    // Shared base: "AB"
    Atom opA = alice.localInsert(0, 'A');
    Atom opB = alice.localInsert(1, 'B');
    bob.remoteMerge(opA); bob.remoteMerge(opB);

    assert(alice.toString() == "AB");
    assert(bob.toString()   == "AB");

    // Alice deletes 'A' (index 0)
    OpID del_a = alice.localDelete(0);

    // Bob simultaneously inserts 'Z' at index 0 (before 'A')
    Atom ins_z = bob.localInsert(0, 'Z');

    // Cross-sync
    alice.remoteMerge(ins_z);
    bob.remoteDelete(del_a);

    std::string at = alice.toString(), bt = bob.toString();
    std::cout << "    Alice: " << at << "\n    Bob  : " << bt << std::endl;
    assert(at == bt && "CONVERGENCE FAILURE: delete vs insert at same pos");
    assert(at.find('B') != std::string::npos && "B must survive");
    assert(at.find('Z') != std::string::npos && "Z must survive");
    assert(at.find('A') == std::string::npos && "A was deleted");
    std::cout << "    PASSED\n" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: Concurrent overlapping formatting marks
// ─────────────────────────────────────────────────────────────────────────────
void test_concurrent_overlapping_marks() {
    std::cout << "[5] Concurrent overlapping formatting marks..." << std::endl;

    Sequence alice(1), bob(2);

    Atom a1 = alice.localInsert(0, 'W');
    Atom a2 = alice.localInsert(1, 'o');
    Atom a3 = alice.localInsert(2, 'r');
    Atom a4 = alice.localInsert(3, 'd');

    bob.remoteMerge(a1); bob.remoteMerge(a2); bob.remoteMerge(a3); bob.remoteMerge(a4);

    alice.addMark(a1.id, a2.id, "bold");
    auto alice_mark = alice.getAllMarks().back();

    bob.addMark(a2.id, a3.id, "italic");
    auto bob_mark = bob.getAllMarks().back();

    alice.remoteMergeMark(bob_mark);
    bob.remoteMergeMark(alice_mark);

    auto alice_styled = alice.getStyledCharacters();
    auto bob_styled   = bob.getStyledCharacters();

    assert(alice_styled.size() == bob_styled.size());
    for (size_t i = 0; i < alice_styled.size(); i++) {
        assert(alice_styled[i].first == bob_styled[i].first);
        assert(alice_styled[i].second.size() == bob_styled[i].second.size());
    }
    std::cout << "    PASSED\n" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: Out-of-order causal delivery — orphan buffering cascade
// ─────────────────────────────────────────────────────────────────────────────
void test_out_of_order_causal_delivery() {
    std::cout << "[6] Out-of-order causal delivery (orphan buffering cascade)..." << std::endl;

    Sequence alice(1), bob(2);

    Atom op_a = alice.localInsert(0, 'A');
    Atom op_b = alice.localInsert(1, 'B');
    Atom op_c = alice.localInsert(2, 'C');

    // Bob receives C → B → A (completely reversed delivery order)
    bob.remoteMerge(op_c);
    bob.remoteMerge(op_b);

    assert(bob.toString().empty());
    assert(bob.getOrphanBufferSize() == 2);

    bob.remoteMerge(op_a); // triggers cascade

    std::cout << "    Bob: " << bob.toString() << std::endl;
    assert(bob.toString() == "ABC" && "Orphan resolution failed");
    assert(bob.getOrphanBufferSize() == 0);
    std::cout << "    PASSED\n" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: Three-way concurrent convergence (Alice, Bob, Carol)
// ─────────────────────────────────────────────────────────────────────────────
void test_three_way_convergence() {
    std::cout << "[7] Three-way concurrent convergence (Alice+Bob+Carol)..." << std::endl;

    Sequence alice(1), bob(2), carol(3);

    // Shared base "OK"
    Atom opO = alice.localInsert(0, 'O');
    Atom opK = alice.localInsert(1, 'K');
    bob.remoteMerge(opO); bob.remoteMerge(opK);
    carol.remoteMerge(opO); carol.remoteMerge(opK);

    // All three concurrently append one character each — offline
    Atom aOp = alice.localInsert(2, 'A');   // Alice appends 'A'
    Atom bOp = bob.localInsert(2, 'B');     // Bob appends 'B'
    Atom cOp = carol.localInsert(2, 'C');   // Carol appends 'C'

    // Full 3-way cross-sync
    alice.remoteMerge(bOp); alice.remoteMerge(cOp);
    bob.remoteMerge(aOp);   bob.remoteMerge(cOp);
    carol.remoteMerge(aOp); carol.remoteMerge(bOp);

    std::string at = alice.toString(), bt = bob.toString(), ct = carol.toString();
    std::cout << "    Alice: " << at << "\n    Bob  : " << bt << "\n    Carol: " << ct << std::endl;

    assert(at == bt && bt == ct && "CONVERGENCE FAILURE: 3-way concurrent append");
    assert(at.size() == 5 && "Should have 5 chars");
    assert(at.find('O') != std::string::npos);
    assert(at.find('K') != std::string::npos);
    assert(at.find('A') != std::string::npos);
    assert(at.find('B') != std::string::npos);
    assert(at.find('C') != std::string::npos);
    std::cout << "    PASSED\n" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8: Convergence stress test — 500 random interleaved ops
// ─────────────────────────────────────────────────────────────────────────────
void test_stress_convergence() {
    std::cout << "[8] Stress convergence (500 random ops, two peers)..." << std::endl;

    srand(42); // deterministic seed for reproducibility

    Sequence alice(1), bob(2);
    std::vector<Atom> alice_ops, bob_ops;

    const char CHARS[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const int  NUM_OPS = 250; // 250 ops per peer = 500 total

    // Generate 250 ops for Alice and 250 for Bob — all offline (no sync during generation)
    for (int i = 0; i < NUM_OPS; i++) {
        char ch = CHARS[rand() % 36];
        size_t pos = alice.toString().empty() ? 0 : rand() % alice.toString().size();
        alice_ops.push_back(alice.localInsert(pos, ch));
    }

    for (int i = 0; i < NUM_OPS; i++) {
        char ch = CHARS[rand() % 36];
        size_t pos = bob.toString().empty() ? 0 : rand() % bob.toString().size();
        bob_ops.push_back(bob.localInsert(pos, ch));
    }

    // Cross-sync all ops
    for (auto& op : bob_ops)   alice.remoteMerge(op);
    for (auto& op : alice_ops) bob.remoteMerge(op);

    std::string at = alice.toString(), bt = bob.toString();
    assert(at == bt && "CONVERGENCE FAILURE: stress test");
    assert(at.size() == 500 && "Should have exactly 500 characters");

    std::cout << "    Both peers converged on " << at.size() << " chars." << std::endl;
    std::cout << "    PASSED\n" << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "     OmniSync Concurrent Robustness Test Suite    " << std::endl;
    std::cout << "==================================================" << std::endl << std::endl;

    test_concurrent_insertions_same_position();
    test_both_type_same_word_simultaneously();
    test_concurrent_deletions();
    test_delete_vs_insert_at_same_position();
    test_concurrent_overlapping_marks();
    test_out_of_order_causal_delivery();
    test_three_way_convergence();
    test_stress_convergence();

    std::cout << "==================================================" << std::endl;
    std::cout << "   ALL 8 CONCURRENT ROBUSTNESS TESTS PASSED!      " << std::endl;
    std::cout << "==================================================" << std::endl;
    return 0;
}
