#include <cassert>
#include <iostream>
#include <sstream>
#include "../include/omnisync/core/sequence.hpp"

using namespace omnisync::core;

void test_basic_marks() {
    std::cout << "Running test_basic_marks..." << std::endl;
    
    Sequence doc(1);
    
    // Insert "Hello"
    Atom a1 = doc.localInsert(0, 'H');
    Atom a2 = doc.localInsert(1, 'e');
    Atom a3 = doc.localInsert(2, 'l');
    Atom a4 = doc.localInsert(3, 'l');
    Atom a5 = doc.localInsert(4, 'o');
    
    assert(doc.toString() == "Hello");
    
    // Add mark bold on "ell" (a2 to a4)
    doc.addMark(a2.id, a4.id, "bold");
    
    auto active_marks = doc.getActiveMarks();
    assert(active_marks.size() == 1);
    assert(active_marks[0].type == "bold");
    assert(active_marks[0].start_id == a2.id);
    assert(active_marks[0].end_id == a4.id);
    assert(!active_marks[0].is_deleted);
    
    // Remove the mark
    doc.removeMark(active_marks[0].id);
    assert(doc.getActiveMarks().empty());
    assert(doc.getAllMarks().size() == 1);
    assert(doc.getAllMarks()[0].is_deleted);
    
    std::cout << "  Passed!" << std::endl;
}

void test_mark_serialization() {
    std::cout << "Running test_mark_serialization..." << std::endl;
    
    Sequence doc(1);
    Atom a1 = doc.localInsert(0, 'A');
    Atom a2 = doc.localInsert(1, 'B');
    
    doc.addMark(a1.id, a2.id, "italic");
    
    // Save to stream
    std::stringstream ss;
    doc.save(ss);
    
    // Load into another sequence
    Sequence doc2(2);
    bool ok = doc2.load(ss);
    assert(ok);
    assert(doc2.toString() == "AB");
    
    auto marks = doc2.getActiveMarks();
    assert(marks.size() == 1);
    assert(marks[0].type == "italic");
    assert(marks[0].start_id == a1.id);
    assert(marks[0].end_id == a2.id);
    
    std::cout << "  Passed!" << std::endl;
}

void test_mark_gc_adjustment() {
    std::cout << "Running test_mark_gc_adjustment..." << std::endl;
    
    Sequence doc(1);
    Atom a1 = doc.localInsert(0, 'H');
    Atom a2 = doc.localInsert(1, 'e');
    Atom a3 = doc.localInsert(2, 'l');
    Atom a4 = doc.localInsert(3, 'l');
    Atom a5 = doc.localInsert(4, 'o');
    
    // Mark "ell" (a2 to a4)
    doc.addMark(a2.id, a4.id, "bold");
    
    // Delete 'e' (a2)
    doc.localDelete(1);
    
    // Perform GC (min_age_threshold = 0 to instantly clean up)
    doc.garbageCollectLocal(0);
    
    // The mark should still be active, but its start_id should have moved to 'l' (a3)
    auto marks = doc.getActiveMarks();
    assert(marks.size() == 1);
    assert(marks[0].type == "bold");
    assert(marks[0].start_id == a3.id); // Adjusted from a2 to a3
    assert(marks[0].end_id == a4.id);   // Remains a4
    
    std::cout << "  Passed!" << std::endl;
}

int main() {
    std::cout << "=== OmniSync Formatting Marks Tests ===" << std::endl;
    
    test_basic_marks();
    test_mark_serialization();
    test_mark_gc_adjustment();
    
    std::cout << "ALL MARKS TESTS PASSED" << std::endl;
    return 0;
}
