#include <cassert>
#include <iostream>
#include <random>
#include <vector>
#include "omnisync/omnisync.hpp"
using namespace omnisync::core;

static void assert_same(const Sequence& a, const Sequence& b) { assert(a.toString() == b.toString()); }

static void test_ids_and_causality() {
    Sequence a(1);
    Atom x = a.localInsert(0, 'x');
    Atom y = a.localInsert(1, 'y');
    assert(x.id.client_id == 1 && y.id.client_id == 1);
    assert(x.id.sequence == 1 && y.id.sequence == 2);
    assert(y.id.clock > x.id.clock);
    assert(a.getVectorClock().get(1) == 2);

    Sequence b(2);
    b.remoteMerge(y); // causal gap: sequence 2 cannot advance prefix yet
    assert(b.getVectorClock().get(1) == 0);
    b.remoteMerge(x);
    assert(b.getVectorClock().get(1) == 2);
    assert(b.toString() == "xy");
}

static void test_concurrent_rga_order_and_duplicates() {
    Sequence a(1), b(2), c(3);
    Atom a1 = a.localInsert(0, 'A');
    Atom b1 = b.localInsert(0, 'B');
    Atom c1 = c.localInsert(0, 'C');
    std::vector<Atom> ops{a1,b1,c1};
    for (auto& op : ops) { a.remoteMerge(op); b.remoteMerge(op); c.remoteMerge(op); }
    for (auto& op : ops) { a.remoteMerge(op); b.remoteMerge(op); c.remoteMerge(op); }
    assert_same(a,b); assert_same(b,c);

    // Force repeated concurrent children of the same stable parent.
    Sequence d(4), e(5);
    Atom parent = d.localInsert(0, 'P');
    e.remoteMerge(parent);
    Atom d1 = d.localInsertAfter(parent.id, 'd');
    Atom e1 = e.localInsertAfter(parent.id, 'e');
    d.remoteMerge(e1);
    e.remoteMerge(d1);
    assert_same(d,e);
}

static void test_delete_before_insert_and_idempotence() {
    Sequence source(1), receiver(2);
    Atom a = source.localInsert(0, 'A');
    Atom b = source.localInsert(1, 'B');
    source.localDeleteId(a.id);
    OpID del = source.getDeleteOperationIds(a.id).back();
    receiver.remoteDelete(a.id, del);
    assert(receiver.toString().empty());
    receiver.remoteDelete(a.id, del);
    receiver.remoteMerge(a);
    receiver.remoteMerge(a);
    receiver.remoteMerge(b);
    assert(receiver.toString() == "B");
    assert(receiver.getTombstoneCount() == 1);
    assert(receiver.getVectorClock().get(1) == 3);
}

static void test_positional_index_after_deletes() {
    Sequence s(1);
    std::vector<Atom> atoms;
    for (char c='a'; c<='z'; ++c) atoms.push_back(s.localInsert(s.visibleLength(), c));
    assert(s.visibleLength() == 26);
    for (size_t i=0;i<26;i++) assert(s.getAtomIdAt(i) == atoms[i].id);
    for (int i=0;i<13;i++) s.localDelete(0);
    assert(s.visibleLength() == 13);
    assert(s.toString() == "nopqrstuvwxyz");
    for (size_t i=0;i<13;i++) assert(s.getAtomIdAt(i) == atoms[i+13].id);
    assert(s.getVisualIndex(atoms[20].id) == 7);
    assert(s.getPredecessor(atoms[20].id) == atoms[19].id);
    assert(s.getSuccessor(atoms[20].id) == atoms[21].id);
}

static void test_gc_is_not_unsafe() {
    Sequence s(1);
    Atom a = s.localInsert(0,'A');
    s.localDeleteId(a.id);
    assert(s.getTombstoneCount() == 1);
    VectorClock frontier(1);
    frontier.update(1, 2);
    // The implementation intentionally refuses physical GC until anchor retirement exists.
    assert(s.garbageCollect(frontier) == 0);
    assert(s.hasAtom(a.id));
    assert(s.toString().empty());
}

int main() {
    test_ids_and_causality();
    test_concurrent_rga_order_and_duplicates();
    test_delete_before_insert_and_idempotence();
    test_positional_index_after_deletes();
    test_gc_is_not_unsafe();
    std::cout << "sequence_core_test: PASS\n";
}
