#include <iostream>
#include <string>
#include <vector>
#include "omnisync/omnisync.hpp"

// A simple simulation demo to prove RGA convergence.
// We will create two "users": Alice and Bob.
// They will type concurrently (at the same time).
// We will show that even if packets arrive out of order, the result is identical.

using namespace omnisync::core;

void printSequence(const std::string& user, const Sequence& seq) {
    std::cout << "[" << user << " View]: " << seq.toString() << std::endl;
}

int main() {
    std::cout << "--- OmniSync Basic Demo: Concurrent Formatting ---" << std::endl;

    // 1. Setup Alice (ID: 1) and Bob (ID: 2)
    Sequence aliceText(1);
    Sequence bobText(2);

    // 2. Initial State: Alice types "Hello"
    // For simplicity, we just insert one by one.
    // In a real app, 'localInsert' returns an Atom to send over network.
    std::vector<Atom> networkQueue;

    std::cout << "\n[Step 1] Alice types 'Hi'" << std::endl;
    networkQueue.push_back(aliceText.localInsert(0, 'H')); // Index 0
    networkQueue.push_back(aliceText.localInsert(1, 'i')); // Index 1

    // Sync Alice -> Bob (Perfect network condition)
    for (const auto& atom : networkQueue) {
        bobText.remoteMerge(atom);
    }
    networkQueue.clear();

    printSequence("Alice", aliceText);
    printSequence("Bob  ", bobText);

    // 3. The Conflict! (Concurrent Editing)
    // Both users start at the end of "Hi".
    // Alice wants to type " World" (Space + World)
    // Bob wants to type " Bob" (Space + Bob)
    
    std::cout << "\n[Step 2] CONFLICT! Alice types ' World', Bob types ' Bob'..." << std::endl;

    // Alice types " World"
    std::vector<Atom> aliceUpdates;
    aliceUpdates.push_back(aliceText.localInsert(2, ' '));
    aliceUpdates.push_back(aliceText.localInsert(3, 'W'));
    aliceUpdates.push_back(aliceText.localInsert(4, 'o'));
    aliceUpdates.push_back(aliceText.localInsert(5, 'r'));
    aliceUpdates.push_back(aliceText.localInsert(6, 'l'));
    aliceUpdates.push_back(aliceText.localInsert(7, 'd'));

    // Bob types " Bob" (Note: He starts at index 2, just like Alice did!)
    std::vector<Atom> bobUpdates;
    bobUpdates.push_back(bobText.localInsert(2, ' '));
    bobUpdates.push_back(bobText.localInsert(3, 'B'));
    bobUpdates.push_back(bobText.localInsert(4, 'o'));
    bobUpdates.push_back(bobText.localInsert(5, 'b'));

    // 4. Simulate Network Sync
    // Alice receives Bob's packets
    std::cout << "Syncing Bob -> Alice..." << std::endl;
    for (const auto& atom : bobUpdates) {
        aliceText.remoteMerge(atom);
    }

    // Bob receives Alice's packets (Maybe slightly out of order? RGA handles it, but let's do simple buffer).
    std::cout << "Syncing Alice -> Bob..." << std::endl;
    for (const auto& atom : aliceUpdates) {
        bobText.remoteMerge(atom);
    }

    // 5. Final Result
    std::cout << "\n[Final Consistency Check]" << std::endl;
    printSequence("Alice", aliceText);
    printSequence("Bob  ", bobText);

    std::string strA = aliceText.toString();
    std::string strB = bobText.toString();

    if (strA == strB) {
        std::cout << "\nSUCCESS: Both clients converged to specific deterministic state!" << std::endl;
        std::cout << "Result: " << strA << std::endl;
    } else {
        std::cout << "\nFAILURE: Desync detected!" << std::endl;
    }

    return 0;
}
