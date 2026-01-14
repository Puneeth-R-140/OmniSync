#include <iostream>
#include <vector>
#include <random>
#include <string>
#include <cassert>
#include <memory>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

// Configuration
const int NUM_USERS = 5;
const int OPS_PER_USER = 500;
const int MAX_STRING_LEN = 10000;

struct Packet {
    int from_user;
    Atom atom;
    bool is_delete; // true = delete, false = insert
    OpID delete_target;
};

// Simulation World (Shared Ptr because Sequence has atomic members, so no copy)
std::vector<std::shared_ptr<Sequence>> users;
std::vector<Packet> network_buffer;

// Random Number Generator
std::mt19937 rng(1337); 

void random_op(int user_idx) {
    auto& seq = *users[user_idx];
    std::string current = seq.toString();
    
    // Decide: Insert or Delete? (70% insert, 30% delete)
    std::uniform_int_distribution<int> dist_type(0, 100);
    bool do_delete = (dist_type(rng) > 70) && (current.length() > 0);

    if (do_delete) {
        // Delete random index
        std::uniform_int_distribution<int> dist_idx(0, current.length() - 1);
        int idx = dist_idx(rng);
        OpID target = seq.localDelete(idx);
        
        // Broadcast
        if (target.clock != 0) {
            network_buffer.push_back({user_idx + 1, Atom(), true, target});
        }
    } else {
        // Insert random char
        std::uniform_int_distribution<int> dist_idx(0, current.length()); // Can insert at end
        std::uniform_int_distribution<int> dist_char(65, 90); // A-Z
        
        int idx = dist_idx(rng);
        char c = (char)dist_char(rng);
        
        Atom new_atom = seq.localInsert(idx, c);
        
        // Broadcast
        network_buffer.push_back({user_idx + 1, new_atom, false, {0,0}});
    }
}

int main() {
    std::cout << "--- OmniSync Fuzz Test: Chaos Mode ---\n";
    std::cout << "Users: " << NUM_USERS << "\n";
    std::cout << "Ops/User: " << OPS_PER_USER << "\n";
    std::cout << "Total Ops: " << NUM_USERS * OPS_PER_USER << "\n";

    // 1. Initialize Users
    users.reserve(NUM_USERS);
    for(int i=0; i<NUM_USERS; i++) {
        users.push_back(std::make_shared<Sequence>(i + 1));
    }

    // 2. Generate Chaos (Local Ops)
    std::cout << "Generating " << NUM_USERS * OPS_PER_USER << " operations...\n";
    for(int i=0; i<OPS_PER_USER; i++) {
        for(int u=0; u<NUM_USERS; u++) {
            random_op(u);
        }
    }

    // 3. The Shuffle (Network Lag)
    std::cout << "Shuffling " << network_buffer.size() << " packets to simulate extreme lag...\n";
    std::shuffle(network_buffer.begin(), network_buffer.end(), rng);

    // 4. Apply Everything to Everyone
    std::cout << "Syncing all users...\n";
    for(int u=0; u<NUM_USERS; u++) {
        auto& seq = *users[u];
        
        for(const auto& packet : network_buffer) {
            // Self-check: Don't merge your own ops again
            // packet.from_user is ClientID (1-based)
            // Current user is u (0-based) -> ClientID u+1
            if (packet.from_user == (u + 1)) continue;

            if (packet.is_delete) {
                seq.remoteDelete(packet.delete_target);
            } else {
                seq.remoteMerge(packet.atom);
            }
        }
    }

    // 5. Verification
    std::cout << "\n--- VERIFICATION ---\n";
    std::string golden = users[0]->toString();
    bool all_match = true;

    for(int u=1; u<NUM_USERS; u++) {
        std::string check = users[u]->toString();
        if (check != golden) {
            all_match = false;
            std::cout << "MISMATCH found!\n";
            std::cout << "User 0 length: " << golden.length() << "\n";
            std::cout << "User " << u << " length: " << check.length() << "\n";
             
            // Print snippet
            std::cout << "User 0 snippet: " << golden.substr(0, 50) << "...\n";
            std::cout << "User " << u << " snippet: " << check.substr(0, 50) << "...\n";
        }
    }

    if (all_match) {
        std::cout << "✅ SUCCESS: All " << NUM_USERS << " users converged identically.\n";
        std::cout << "Final Content Length: " << golden.length() << "\n";
    } else {
        std::cout << "❌ FAILURE: Consistency broken.\n";
        return 1;
    }

    return 0;
}
