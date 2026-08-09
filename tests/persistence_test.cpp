#include <cassert>
#include <iostream>
#include <sstream>
#include "omnisync/omnisync.hpp"
using namespace omnisync::core;

static void test_roundtrip() {
    Sequence a(7);
    Atom a1=a.localInsert(0,'A');
    Atom a2=a.localInsert(1,'B');
    a.localDeleteId(a1.id);
    a.addMark(a2.id,a2.id,"bold");
    std::stringstream ss(std::ios::in|std::ios::out|std::ios::binary);
    a.save(ss);
    Sequence b(99);
    ss.seekg(0);
    assert(b.load(ss));
    assert(b.toString()==a.toString());
    assert(b.getTombstoneCount()==a.getTombstoneCount());
    assert(b.getAllMarks().size()==a.getAllMarks().size());
    assert(b.getVectorClock().get(7)==a.getVectorClock().get(7));
    Atom fresh=b.localInsert(b.visibleLength(),'C');
    assert(fresh.id.client_id==7);
    assert(fresh.id.sequence > 2);
}

static void test_corruption_rejected_without_destroying_live_state() {
    Sequence live(1);
    live.localInsert(0,'L');
    std::stringstream bad(std::ios::in|std::ios::out|std::ios::binary);
    bad.write("OMNI",4); unsigned char v=6; bad.write((char*)&v,1); bad.write("bad",3); bad.seekg(0);
    assert(!live.load(bad));
    assert(live.toString()=="L");
}

static void test_truncation_and_trailing_bytes() {
    Sequence a(1); a.localInsert(0,'x');
    std::stringstream ss(std::ios::in|std::ios::out|std::ios::binary); a.save(ss);
    std::string bytes=ss.str();
    for(size_t n=0;n<bytes.size();++n){ std::stringstream t(std::ios::in|std::ios::out|std::ios::binary); t.write(bytes.data(),(std::streamsize)n); t.seekg(0); Sequence b(2); assert(!b.load(t)); }
    bytes.push_back('X'); std::stringstream extra(std::ios::in|std::ios::out|std::ios::binary); extra.write(bytes.data(),bytes.size()); extra.seekg(0); Sequence c(2); assert(!c.load(extra));
}

int main(){ test_roundtrip(); test_corruption_rejected_without_destroying_live_state(); test_truncation_and_trailing_bytes(); std::cout<<"persistence_test: PASS\n"; }
