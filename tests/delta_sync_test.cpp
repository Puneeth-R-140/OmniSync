#include <iostream>
#include <cassert>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

int main() {
    std::cout << "--- OmniSync Delta Sync Test ---\n\n";

    // Scenario: Alice and Bob are collaborating
    // They start with same document, then make changes
    // We test that delta sync only sends the new changes

    // 1. Both start with "Hello"
    Sequence alice(1);
    Sequence bob(2);
    
    std::cout << "Phase 1: Initial sync (both type 'Hello')\n";
    std::vector<Atom> alice_ops_1;
    alice_ops_1.push_back(alice.localInsert(0, 'H'));
    alice_ops_1.push_back(alice.localInsert(1, 'e'));
    alice_ops_1.push_back(alice.localInsert(2, 'l'));
    alice_ops_1.push_back(alice.localInsert(3, 'l'));
    alice_ops_1.push_back(alice.localInsert(4, 'o'));
    
    std::cout << "  Alice: " << alice.toString() << "\n";
    
    // Bob applies Alice's operations
    for (const auto& atom : alice_ops_1) {
        bob.remoteMerge(atom);
    }
    
    std::cout << "  Bob (after merge): " << bob.toString() << "\n";
    assert(alice.toString() == bob.toString());
    
    // 2. Get Bob's state BEFORE new edits
    VectorClock bob_state_before = bob.getVectorClock();
    std::cout << "\nPhase 2: Alice adds ' World' (6 new chars)\n";
    
    // Alice makes new edits
    std::vector<Atom> alice_ops_2;
    alice_ops_2.push_back(alice.localInsert(5, ' '));
    alice_ops_2.push_back(alice.localInsert(6, 'W'));
    alice_ops_2.push_back(alice.localInsert(7, 'o'));
    alice_ops_2.push_back(alice.localInsert(8, 'r'));
    alice_ops_2.push_back(alice.localInsert(9, 'l'));
    alice_ops_2.push_back(alice.localInsert(10, 'd'));
    
    std::cout << "  Alice: " << alice.toString() << "\n";

    
    
    // 3. NAIVE SYNC: Send ALL operations (wasteful)
    std::cout << "\nNaive Sync:\n";
    std::vector<Atom> all_ops;
    for (const auto& atom : alice_ops_1) all_ops.push_back(atom);
    for (const auto& atom : alice_ops_2) all_ops.push_back(atom);
    std::cout << "  Would send " << all_ops.size() << " operations (entire document)\n";
    
    // 4. DELTA SYNC: Only send what Bob is missing
    std::cout << "\nDelta Sync:\n";
    std::vector<Atom> delta = alice.getDelta(bob_state_before);
    std::cout << "  Sending " << delta.size() << " operations (only new edits)\n";
    
    assert(delta.size() == 6); // Only the new " World" edits
    
    // 5. Bob applies delta
    bob.applyDelta(delta);
    std::cout << "\n  Bob (after delta): " << bob.toString() << "\n";
    
    assert(alice.toString() == bob.toString());
    assert(bob.toString() == "Hello World");
    
    std::cout << "\nPhase 3: Alice deletes 'W' after Bob already has it\n";

    VectorClock bob_state_before_delete = bob.getVectorClock();

    alice.localDelete(6);

    assert(alice.toString() == "Hello orld");
    
    std::vector<Atom> delete_delta =
    alice.getDelta(bob_state_before_delete);

    std::cout << "  Alice: " << alice.toString() << "\n";
    std::cout << "  Deletion delta size: " << delete_delta.size() << "\n";

    assert(!delete_delta.empty());

    bob.applyDelta(delete_delta);

    std::cout << "  Bob: " << bob.toString() << "\n";

    assert(bob.toString() == "Hello orld");
    std::cout << "\nPhase 3b: Repeated deletion delivery\n";

    bob.applyDelta(delete_delta);
    bob.applyDelta(delete_delta);

    assert(bob.toString() == "Hello orld");

    std::cout << "  Bob after repeated deletion: "<< bob.toString() << "\n";
    assert(alice.toString() == bob.toString());
    // 6. Test with concurrent edits

    std::cout << "\nPhase 4: Delete before insertion reaches Bob\n";

    Sequence charlie(3);
    Sequence david(4);

    // Charlie creates X
    charlie.localInsert(0, 'X');

    // Charlie deletes X before David ever receives it
    charlie.localDelete(0);

    assert(charlie.toString().empty());

    // Charlie sends the delta to David
    VectorClock david_state = david.getVectorClock();
    std::vector<Atom> delete_before_insert_delta =charlie.getDelta(david_state);

    std::cout << "  Delta size: "<< delete_before_insert_delta.size() << "\n";

    assert(!delete_before_insert_delta.empty());

    // David receives the insertion + tombstone
    david.applyDelta(delete_before_insert_delta);

    std::cout << "  David: \"" << david.toString() << "\"\n";

    assert(david.toString().empty());

    std::cout << "\nPhase 5: Concurrent edits\n";
    
    VectorClock alice_state = alice.getVectorClock();
    VectorClock bob_state = bob.getVectorClock();
    
    // Alice adds "!"
    alice.localInsert(11, '!');
    std::cout << "  Alice: " << alice.toString() << "\n";
    
    // Bob adds "?" (concurrent with Alice's "!")
    bob.localInsert(11, '?');
    std::cout << "  Bob: " << bob.toString() << " (before receiving Alice's edit)\n";
    
    // Exchange deltas
    std::vector<Atom> alice_delta = alice.getDelta(bob_state);
    std::vector<Atom> bob_delta = bob.getDelta(alice_state);
    
    std::cout << "\n  Alice sends " << alice_delta.size() << " operation to Bob\n";
    std::cout << "  Bob sends " << bob_delta.size() << " operation to Alice\n";
    
    bob.applyDelta(alice_delta);
    alice.applyDelta(bob_delta);
    
    std::cout << "\n  Alice (final): " << alice.toString() << "\n";
    std::cout << "  Bob (final): " << bob.toString() << "\n";
    
    // They should converge (order determined by OpID)
    assert(alice.toString() == bob.toString());
    
    std::cout << "\nSUCCESS: Delta Sync Verified!\n";
    std::cout << "   - Delta sync sends only missing operations\n";
    std::cout << "   - Concurrent edits merged correctly\n";
    std::cout << "   - Full convergence maintained\n";
    
    return 0;
}
