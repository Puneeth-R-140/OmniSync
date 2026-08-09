#include <cassert>
#include <iostream>
#include <vector>
#include "omnisync/omnisync.hpp"
using namespace omnisync::core;

static void test_incremental_delta() {
    Sequence a(1), b(2);
    std::vector<Atom> first;
    for (char c='a'; c<='j'; ++c) first.push_back(a.localInsert(a.visibleLength(), c));
    auto d1 = a.getDelta(b.getVectorClock());
    assert(d1.size() == 10);
    b.applyDelta(d1);
    assert(b.toString() == "abcdefghij");
    assert(a.getDelta(b.getVectorClock()).empty());

    for (char c='k'; c<='z'; ++c) a.localInsert(a.visibleLength(), c);
    auto d2 = a.getDelta(b.getVectorClock());
    assert(d2.size() == 16);
    b.applyDelta(d2);
    assert(b.toString() == a.toString());

    // Replay must be idempotent.
    b.applyDelta(d2);
    assert(b.toString() == a.toString());
}

static void test_delete_delta() {
    Sequence a(1), b(2);
    Atom x = a.localInsert(0,'x');
    b.applyDelta(a.getDelta(b.getVectorClock()));
    a.localDeleteId(x.id);
    auto delta = a.getDelta(b.getVectorClock());
    assert(!delta.empty());
    b.applyDelta(delta);
    assert(b.toString().empty());
    assert(b.getTombstoneCount() == 1);
}

static void test_gap_delta_delivery() {
    Sequence a(1), b(2);
    Atom one=a.localInsert(0,'1');
    Atom two=a.localInsert(1,'2');
    Atom three=a.localInsert(2,'3');
    b.remoteMerge(three);
    assert(b.getVectorClock().get(1)==0);
    b.remoteMerge(one);
    assert(b.getVectorClock().get(1)==1);
    b.remoteMerge(two);
    assert(b.getVectorClock().get(1)==3);
    assert(b.toString()=="123");
}

int main(){ test_incremental_delta(); test_delete_delta(); test_gap_delta_delivery(); std::cout<<"delta_sync_test: PASS\n"; }
