#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "omnisync/omnisync.hpp"
using namespace omnisync::core;
using namespace omnisync::network;

static void test_atom_roundtrip() {
    Atom a{OpID{0x1111111111111111ULL,0x2222222222222222ULL,0x3333333333333333ULL},
           OpID{4,5,6},'Z'};
    a.is_deleted=true;
    a.delete_operation_ids={OpID{7,8,9},OpID{10,11,12}};
    auto bytes=BinaryPacker::pack(a);
    assert(bytes.size()==BinaryPacker::packedSize(a));
    Atom b; assert(BinaryPacker::unpack(bytes,b));
    assert(b.id==a.id && b.origin==a.origin && b.content==a.content && b.is_deleted);
    assert(b.delete_operation_ids==a.delete_operation_ids);
    Atom c; assert(VLEPacker::unpack(VLEPacker::pack(a),c)); assert(c.id==a.id);
}

static void test_malformed() {
    Atom out;
    assert(!BinaryPacker::unpack({},out));
    std::vector<uint8_t> bad={'O','X',2}; assert(!BinaryPacker::unpack(bad,out));
    auto valid=BinaryPacker::pack(Atom{OpID{1,2,3},OpID{},'a'});
    for(size_t n=0;n<valid.size();++n){ std::vector<uint8_t> t(valid.begin(),valid.begin()+n); assert(!BinaryPacker::unpack(t,out)); }
    auto trailing=valid; trailing.push_back(0); assert(!BinaryPacker::unpack(trailing,out));
    auto invalid_bool=valid; invalid_bool[invalid_bool.size()-2]=2; assert(!BinaryPacker::unpack(invalid_bool,out));
}

static void test_large_ids_and_vle_edges() {
    Atom a{OpID{UINT64_MAX,UINT64_MAX-1,UINT64_MAX-2},OpID{UINT64_MAX-3,UINT64_MAX-4,UINT64_MAX-5},'Q'};
    auto b=BinaryPacker::pack(a); Atom r; assert(BinaryPacker::unpack(b,r)); assert(r.id==a.id && r.origin==a.origin);
    std::vector<uint8_t> overflow(11,0x80); uint64_t value=0; size_t pos=0; assert(!VLEEncoding::decodeUInt64(overflow,pos,value));
}

int main(){test_atom_roundtrip();test_malformed();test_large_ids_and_vle_edges();std::cout<<"network_protocol_test: PASS\n";}
