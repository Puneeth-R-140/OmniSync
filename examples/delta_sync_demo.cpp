/**
 * Delta Sync Example
 *
 * Demonstrates efficient synchronization by sending only operations that the
 * receiving peer has not yet observed, then applying VLE compression to the
 * resulting delta payload.
 */

#include <cassert>
#include <iostream>
#include <vector>

#include "omnisync/omnisync.hpp"

using namespace omnisync::core;
using namespace omnisync::network;

int main() {
    std::cout << "=== Delta Sync Example ===\n\n";

    Sequence peer1(1);
    Sequence peer2(2);

    std::cout << "Peer 1 creates initial document...\n";
    for (char c : std::string("The quick brown fox")) {
        peer1.localInsert(peer1.toString().size(), c);
    }

    std::cout << "Peer 1 content: \"" << peer1.toString() << "\"\n";

    std::cout << "\n--- Initial Sync (full history once) ---\n";
    std::vector<Atom> initial_sync = peer1.getDelta(VectorClock());
    peer2.applyDelta(initial_sync);

    std::cout << "Initial atoms sent: " << initial_sync.size() << "\n";
    std::cout << "Peer 2 content: \"" << peer2.toString() << "\"\n";
    assert(peer1.toString() == peer2.toString());

    VectorClock peer2_state_before = peer2.getVectorClock();

    std::cout << "\n--- Peer 1 makes new edits ---\n";
    peer1.localInsert(peer1.toString().size(), ' ');
    peer1.localInsert(peer1.toString().size(), 'j');
    peer1.localInsert(peer1.toString().size(), 'u');
    peer1.localInsert(peer1.toString().size(), 'm');
    peer1.localInsert(peer1.toString().size(), 'p');

    std::cout << "Peer 1 content now: \"" << peer1.toString() << "\"\n";

    std::cout << "\n--- Delta Sync ---\n";
    std::vector<Atom> delta = peer1.getDelta(peer2_state_before);
    std::vector<Atom> full_history = peer1.getDelta(VectorClock());

    std::cout << "Delta atoms: " << delta.size() << "\n";
    std::cout << "Full history atoms: " << full_history.size() << "\n";

    double saved = 0.0;
    if (!full_history.empty()) {
        saved = 100.0 * (1.0 - static_cast<double>(delta.size()) / static_cast<double>(full_history.size()));
    }
    std::cout << "Atom transfer reduction: " << saved << "%\n";

    peer2.applyDelta(delta);
    std::cout << "Peer 2 after delta: \"" << peer2.toString() << "\"\n";
    std::cout << "Convergence: " << (peer1.toString() == peer2.toString() ? "YES" : "NO") << "\n";
    assert(peer1.toString() == peer2.toString());

    std::cout << "\n--- VLE Compression over Delta ---\n";
    size_t fixed_bytes = 0;
    size_t vle_bytes = 0;

    for (const auto& atom : delta) {
        fixed_bytes += BinaryPacker::pack(atom).size();
        vle_bytes += VLEPacker::pack(atom).size();
    }

    std::cout << "Delta fixed-size bytes: " << fixed_bytes << "\n";
    std::cout << "Delta VLE bytes: " << vle_bytes << "\n";
    if (fixed_bytes > 0) {
        std::cout << "VLE compression: "
                  << (100.0 * (1.0 - static_cast<double>(vle_bytes) / static_cast<double>(fixed_bytes)))
                  << "%\n";
    }

    return 0;
}
