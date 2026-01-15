#include <iostream>
#include <fstream>
#include <cassert>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

int main() {
    std::cout << "--- OmniSync Persistence Test ---\n";

    // 1. Create and Populate Document 1
    Sequence doc1(1);
    doc1.localInsert(0, 'A');
    doc1.localInsert(1, 'B');
    doc1.localInsert(2, 'C');
    doc1.localDelete(1); // Delete 'B', result "AC"
    
    std::cout << "Doc1 Content: " << doc1.toString() << "\n";
    assert(doc1.toString() == "AC");

    // 2. Save
    {
        std::ofstream outfile("save_test.os", std::ios::binary);
        if (!outfile) {
            std::cerr << "Failed to open file for writing\n";
            return 1;
        }
        doc1.save(outfile);
        outfile.close();
        std::cout << "Saved to 'save_test.os'\n";
    }

    // 3. Load into Document 2
    Sequence doc2(2); // Different Client ID
    {
        std::ifstream infile("save_test.os", std::ios::binary);
        if (!infile) {
             std::cerr << "Failed to open file for reading\n";
             return 1;
        }
        bool success = doc2.load(infile);
        infile.close();
        if (!success) {
            std::cerr << "Load failed (Magic header mismatch?)\n";
            return 1;
        }
        std::cout << "Loaded into Doc2\n";
    }

    // 4. Verify Content
    std::cout << "Doc2 Content: " << doc2.toString() << "\n";
    assert(doc2.toString() == "AC");
    assert(doc2.toString() == doc1.toString());

    // 5. Verify Index Integrity (Can we insert after loaded atoms?)
    // Try to insert 'D' at end
    doc2.localInsert(2, 'D'); 
    std::cout << "Doc2 Modified: " << doc2.toString() << "\n";
    assert(doc2.toString() == "ACD");

    std::cout << "SUCCESS: Save/Load Verified.\n";
    return 0;
}
