/**
 * Save and Load Example
 * 
 * Demonstrates document persistence - saving CRDT state to disk and
 * loading it back while maintaining full CRDT properties.
 * 
 * Author: Puneeth R
 */

#include <iostream>
#include <fstream>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

int main() {
    std::cout << "=== Save and Load Example ===\n\n";
    
    // Create and populate a document
    std::cout << "--- Creating Document ---\n";
    Sequence doc1(1);
    
    std::string content = "OmniSync is a CRDT library for C++";
    for (size_t i = 0; i < content.size(); i++) {
        doc1.localInsert(i, content[i]);
    }
    
    std::cout << "Created: \"" << doc1.toString() << "\"\n";
    std::cout << "Atoms: " << doc1.getAtomCount() << "\n";
    
    // Make some deletions to create tombstones
    doc1.localDelete(0);  // Delete 'O'
    doc1.localDelete(0);  // Delete 'm'
    
    std::cout << "After edits: \"" << doc1.toString() << "\"\n";
    std::cout << "Atoms: " << doc1.getAtomCount() 
              << " (including " << doc1.getTombstoneCount() << " tombstones)\n\n";
    
    // Save to file
    std::cout << "--- Saving to File ---\n";
    std::ofstream out_file("document_backup.osd", std::ios::binary);
    if (!out_file) {
        std::cerr << "Error opening file for writing\n";
        return 1;
    }
    
    doc1.save(out_file);
    out_file.close();
    
    std::cout << "Document saved to: document_backup.osd\n\n";
    
    // Load into a new document
    std::cout << "--- Loading from File ---\n";
    std::ifstream in_file("document_backup.osd", std::ios::binary);
    if (!in_file) {
        std::cerr << "Error opening file for reading\n";
        return 1;
    }
    
    Sequence doc2 = Sequence::load(in_file);
    in_file.close();
    
    std::cout << "Document loaded\n";
    std::cout << "Loaded content: \"" << doc2.toString() << "\"\n";
    std::cout << "Loaded atoms: " << doc2.getAtomCount() << "\n";
    std::cout << "Loaded tombstones: " << doc2.getTombstoneCount() << "\n\n";
    
    // Verify integrity
    std::cout << "--- Verification ---\n";
    bool content_matches = (doc1.toString() == doc2.toString());
    bool atom_count_matches = (doc1.getAtomCount() == doc2.getAtomCount());
    bool tombstone_count_matches = (doc1.getTombstoneCount() == doc2.getTombstoneCount());
    
    std::cout << "Content matches: " << (content_matches ? "YES" : "NO") << "\n";
    std::cout << "Atom count matches: " << (atom_count_matches ? "YES" : "NO") << "\n";
    std::cout << "Tombstone count matches: " << (tombstone_count_matches ? "YES" : "NO") << "\n\n";
    
    // Demonstrate that loaded document is fully functional
    std::cout << "--- Testing Loaded Document ---\n";
    doc2.localInsert(0, '[');
    doc2.localInsert(doc2.toString().size(), ']');
    
    std::cout << "After new edits: \"" << doc2.toString() << "\"\n";
    std::cout << "Total atoms now: " << doc2.getAtomCount() << "\n";
    
    // Can still merge with original
    std::cout << "\n--- Merging with Original ---\n";
    
    // Get the new operations from doc2
    VectorClock doc1_clock = doc1.getVectorClock();
    DeltaState delta = doc2.getDelta(doc1_clock);
    
    std::cout << "Delta has " << delta.new_atoms.size() << " new atoms\n";
    
    for (const auto& atom : delta.new_atoms) {
        doc1.remoteMerge(atom);
    }
    
    std::cout << "Original after merge: \"" << doc1.toString() << "\"\n";
    std::cout << "Convergence: " << (doc1.toString() == doc2.toString() ? "YES" : "NO") << "\n";
    
    return 0;
}
