/**
 * Save and Load Example
 *
 * Demonstrates document persistence and post-restore synchronization using the
 * current Sequence API.
 */

#include <cassert>
#include <fstream>
#include <iostream>

#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

int main() {
    std::cout << "=== Save and Load Example ===\n\n";

    std::cout << "--- Creating Document ---\n";
    Sequence doc1(1);

    const std::string content = "OmniSync is a CRDT library for C++";
    for (size_t i = 0; i < content.size(); ++i) {
        doc1.localInsert(i, content[i]);
    }

    doc1.localDelete(0); // delete 'O'
    doc1.localDelete(0); // delete 'm'

    MemoryStats stats1 = doc1.getMemoryStats();
    std::cout << "Doc1 visible content: \"" << doc1.toString() << "\"\n";
    std::cout << "Doc1 atoms: " << stats1.atom_count
              << " (tombstones: " << stats1.tombstone_count << ")\n\n";

    std::cout << "--- Saving to File ---\n";
    std::ofstream out_file("document_backup.osd", std::ios::binary);
    if (!out_file) {
        std::cerr << "Error opening file for writing\n";
        return 1;
    }
    doc1.save(out_file);
    out_file.close();
    std::cout << "Saved: document_backup.osd\n\n";

    std::cout << "--- Loading from File ---\n";
    Sequence doc2(2);
    std::ifstream in_file("document_backup.osd", std::ios::binary);
    if (!in_file) {
        std::cerr << "Error opening file for reading\n";
        return 1;
    }
    bool loaded = doc2.load(in_file);
    in_file.close();
    if (!loaded) {
        std::cerr << "Load failed\n";
        return 1;
    }

    MemoryStats stats2 = doc2.getMemoryStats();
    std::cout << "Doc2 visible content: \"" << doc2.toString() << "\"\n";
    std::cout << "Doc2 atoms: " << stats2.atom_count
              << " (tombstones: " << stats2.tombstone_count << ")\n\n";

    std::cout << "--- Verification ---\n";
    assert(doc1.toString() == doc2.toString());
    assert(stats1.atom_count == stats2.atom_count);
    assert(stats1.tombstone_count == stats2.tombstone_count);
    std::cout << "Round-trip integrity: YES\n\n";

    std::cout << "--- Editing Restored Document ---\n";
    doc2.localInsert(0, '[');
    doc2.localInsert(doc2.toString().size(), ']');
    std::cout << "Doc2 after edits: \"" << doc2.toString() << "\"\n";

    std::cout << "\n--- Delta Merge Back to Doc1 ---\n";
    VectorClock doc1_state = doc1.getVectorClock();
    std::vector<Atom> delta = doc2.getDelta(doc1_state);
    std::cout << "Delta atoms: " << delta.size() << "\n";

    doc1.applyDelta(delta);

    std::cout << "Doc1 after merge: \"" << doc1.toString() << "\"\n";
    std::cout << "Convergence: " << (doc1.toString() == doc2.toString() ? "YES" : "NO") << "\n";
    assert(doc1.toString() == doc2.toString());

    return 0;
}
