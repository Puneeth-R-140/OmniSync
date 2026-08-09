#include <cassert>
#include <iostream>
#include <sstream>
#include "omnisync/omnisync.hpp"
using namespace omnisync::core;

static void test_mark_replication_and_delete() {
    Sequence a(1), b(2);
    Atom x=a.localInsert(0,'x'); Atom y=a.localInsert(1,'y');
    a.addMark(x.id,y.id,"bold");
    auto marks=a.getAllMarks(); assert(marks.size()==1);
    b.applyDelta(a.getDelta(b.getVectorClock()));
    b.remoteMergeMark(a.getAllMarks().front());
    assert(b.getAllMarks().size()==1);
    assert(b.getActiveMarks().size()==1);
    auto m=b.getActiveMarks().front();
    b.removeMark(m.id);
    a.remoteMergeMark(b.getAllMarks().front());
    assert(a.getActiveMarks().empty());
}

static void test_overlapping_marks_and_stable_ids() {
    Sequence s(1);
    Atom a=s.localInsert(0,'a'), b=s.localInsert(1,'b'), c=s.localInsert(2,'c');
    s.addMark(a.id,b.id,"bold"); s.addMark(b.id,c.id,"italic");
    auto marks=s.getActiveMarks(); assert(marks.size()==2);
    auto styled=s.getStyledCharacters(); assert(styled.size()==3);
    assert(styled[0].second.size()==1); assert(styled[1].second.size()==2); assert(styled[2].second.size()==1);
    s.localDelete(1);
    auto after=s.getStyledCharacters(); assert(after.size()==2);
}

static void test_mark_deletion_persistence() {
    Sequence a(1); Atom x=a.localInsert(0,'x'); a.addMark(x.id,x.id,"red"); auto m=a.getAllMarks().front(); a.removeMark(m.id);
    std::stringstream ss(std::ios::in|std::ios::out|std::ios::binary); a.save(ss); ss.seekg(0);
    Sequence b(2); assert(b.load(ss)); assert(b.getAllMarks().size()==1); assert(b.getActiveMarks().empty());
}

int main(){test_mark_replication_and_delete();test_overlapping_marks_and_stable_ids();test_mark_deletion_persistence();std::cout<<"marks_test: PASS\n";}
