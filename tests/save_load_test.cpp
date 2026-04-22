#include <iostream>
#include <fstream>
#include <cassert>
#include <vector>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

static void test_invalid_magic() {
    std::ofstream bad("bad_magic.os", std::ios::binary);
    bad.write("BADD", 4);
    uint8_t ver = 2;
    bad.write((char*)&ver, 1);
    bad.close();

    Sequence doc(9);
    std::ifstream in("bad_magic.os", std::ios::binary);
    assert(in && "failed to open bad_magic.os");
    bool ok = doc.load(in);
    in.close();
    assert(!ok && "load should fail when magic header is invalid");
}

static void test_unsupported_version() {
    std::ofstream bad("bad_version.os", std::ios::binary);
    bad.write("OMNI", 4);
    uint8_t ver = 9; // unsupported
    bad.write((char*)&ver, 1);
    bad.close();

    Sequence doc(10);
    std::ifstream in("bad_version.os", std::ios::binary);
    assert(in && "failed to open bad_version.os");
    bool ok = doc.load(in);
    in.close();
    assert(!ok && "load should fail for unsupported save format version");
}

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

    // 6. Negative tests for malformed persistence input
    test_invalid_magic();
    test_unsupported_version();
    std::cout << "Malformed-input checks: PASS\n";

    std::cout << "SUCCESS: Save/Load Verified.\n";
    return 0;
}
