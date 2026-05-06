#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "../include/omnisync/core/sequence.hpp"
#include "../include/omnisync/network/binary_packer.hpp"
#include "../include/omnisync/network/vle_encoding.hpp"

using namespace omnisync;

// Test 1: BinaryPacker truncated input
void test_binary_packer_truncated() {
    std::cout << "Test: BinaryPacker truncated input..." << std::endl;
    
    std::vector<uint8_t> truncated(10); // Less than 34 bytes
    BinaryPacker packer;
    
    try {
        packer.unpack(truncated.data(), truncated.size());
        assert(false && "Should have rejected truncated input");
    } catch (const std::exception&) {
        // Expected
    }
}

// Test 2: BinaryPacker roundtrip (baseline)
void test_binary_packer_roundtrip() {
    std::cout << "Test: BinaryPacker roundtrip..." << std::endl;
    
    Atom atom;
    atom.opID.clientID = 1;
    atom.opID.clock = 100;
    atom.value = 'A';
    atom.deleted = false;
    
    BinaryPacker packer;
    std::vector<uint8_t> buffer = packer.pack(atom);
    assert(buffer.size() == 34);
    
    Atom restored = packer.unpack(buffer.data(), buffer.size());
    assert(restored.opID.clientID == 1);
    assert(restored.opID.clock == 100);
    assert(restored.value == 'A');
    assert(!restored.deleted);
}

// Test 3: VLEPacker truncated input
void test_vle_packer_truncated() {
    std::cout << "Test: VLEPacker truncated input..." << std::endl;
    
    // VLE format: clientID (VLE), clock (VLE), value (1 byte), deleted (1 byte)
    // A valid complete VLE would be at least 4 bytes: 1 + 1 + 1 + 1
    // Let's make a truncated one that ends mid-field
    std::vector<uint8_t> truncated = {0x85, 0x80, 0x80}; // Continuation bits but incomplete
    
    VLEPacker packer;
    try {
        packer.unpack(truncated.data(), truncated.size());
        assert(false && "Should have rejected truncated VLE");
    } catch (const std::exception&) {
        // Expected
    }
}

// Test 4: VLE overflow-like input
void test_vle_overflow_input() {
    std::cout << "Test: VLE overflow-like input..." << std::endl;
    
    // 11 bytes of continuation bits should be rejected (overflow)
    std::vector<uint8_t> overflow;
    for (int i = 0; i < 11; i++) {
        overflow.push_back(0x80 | (i % 128)); // Continuation bit set
    }
    
    VLEEncoding vle;
    try {
        size_t pos = 0;
        vle.decodeVLE(overflow.data(), overflow.size(), pos);
        assert(false && "Should have rejected overflow VLE");
    } catch (const std::exception&) {
        // Expected
    }
}

// Test 5: VLE stream read truncated
void test_vle_stream_truncated() {
    std::cout << "Test: VLE stream read truncated..." << std::endl;
    
    // Create a stream with incomplete VLE
    std::vector<uint8_t> stream = {0xFF, 0xFF, 0xFF}; // Incomplete multi-byte VLE
    
    VLEEncoding vle;
    try {
        size_t pos = 0;
        vle.decodeVLE(stream.data(), stream.size(), pos);
        // If we reach here, the decoder ran out of bytes
        assert(pos > stream.size() || stream[stream.size()-1] < 128);
    } catch (const std::exception&) {
        // Expected for truncated input
    }
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
