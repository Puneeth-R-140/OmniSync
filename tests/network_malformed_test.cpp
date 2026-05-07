#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "../include/omnisync/core/crdt_atom.hpp"
#include "../include/omnisync/network/binary_packer.hpp"
#include "../include/omnisync/network/vle_encoding.hpp"

using namespace omnisync::core;
using namespace omnisync::network;

// Test 1: BinaryPacker truncated input
void test_binary_packer_truncated() {
    std::cout << "Test: BinaryPacker truncated input..." << std::endl;
    
    std::vector<uint8_t> truncated(10); // Less than 34 bytes
    Atom out_atom;
    
    bool result = BinaryPacker::unpack(truncated, out_atom);
    assert(!result && "Should have rejected truncated input");
}

// Test 2: BinaryPacker roundtrip (baseline)
void test_binary_packer_roundtrip() {
    std::cout << "Test: BinaryPacker roundtrip..." << std::endl;
    
    Atom atom;
    atom.id.client_id = 1;
    atom.id.clock = 100;
    atom.origin.client_id = 1;
    atom.origin.clock = 100;
    atom.content = 'A';
    atom.is_deleted = false;
    
    std::vector<uint8_t> buffer = BinaryPacker::pack(atom);
    assert(buffer.size() == 34);
    
    Atom restored;
    bool result = BinaryPacker::unpack(buffer, restored);
    assert(result);
    assert(restored.id.client_id == 1);
    assert(restored.id.clock == 100);
    assert(restored.content == 'A');
    assert(!restored.is_deleted);
}

// Test 3: VLEPacker truncated input
void test_vle_packer_truncated() {
    std::cout << "Test: VLEPacker truncated input..." << std::endl;
    
    // Create a truncated VLE buffer
    std::vector<uint8_t> truncated = {0x85, 0x80, 0x80}; // Continuation bits but incomplete
    Atom out_atom;
    
    bool result = VLEPacker::unpack(truncated, out_atom);
    assert(!result && "Should have rejected truncated VLE");
}

// Test 4: VLE overflow-like input
void test_vle_overflow_input() {
    std::cout << "Test: VLE overflow-like input..." << std::endl;
    
    // 11 bytes of continuation bits should be rejected (overflow, max is 10 bytes for 64-bit)
    std::vector<uint8_t> overflow;
    for (int i = 0; i < 11; i++) {
        overflow.push_back(0x80 | (i % 128)); // Continuation bit set
    }
    
    uint64_t out_value;
    size_t pos = 0;
    bool result = VLEEncoding::decodeUInt64(overflow, pos, out_value);
    assert(!result && "Should have rejected overflow VLE");
}

// Test 5: VLE stream read truncated
void test_vle_stream_truncated() {
    std::cout << "Test: VLE stream read truncated..." << std::endl;
    
    // Create a stream with incomplete VLE (all continuation bits, no terminal)
    std::vector<uint8_t> stream = {0xFF, 0xFF}; // Incomplete multi-byte VLE
    
    uint64_t out_value;
    size_t pos = 0;
    bool result = VLEEncoding::decodeUInt64(stream, pos, out_value);
    
    // Should fail because we hit end of buffer before finding a byte without continuation bit
    assert(!result && "Should have rejected truncated stream");
}

int main() {
    std::cout << "--- Network Malformed Input Tests ---" << std::endl;
    
    try {
        test_binary_packer_truncated();
        test_binary_packer_roundtrip();
        test_vle_packer_truncated();
        test_vle_overflow_input();
        test_vle_stream_truncated();
        
        std::cout << "ALL NETWORK MALFORMED TESTS PASSED" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
