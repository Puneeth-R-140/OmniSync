#include <iostream>
#include <vector>
#include <random>
#include <string>
#include <cassert>
#include <memory>
#include <cstdlib>
#include "omnisync/omnisync.hpp"

using namespace omnisync::core;

int NUM_USERS = 5;
int OPS_PER_USER = 2000;
unsigned int RANDOM_SEED = 1337;

struct Packet {
    int from_user;
    Atom atom;
    bool is_delete;
    OpID delete_target;
    OpID delete_operation_id;
};

std::vector<std::shared_ptr<Sequence>> users;
std::vector<Packet> network_buffer;

std::mt19937 rng(RANDOM_SEED);

void random_op(int user_idx) {
    auto& seq = *users[user_idx];
    std::string current = seq.toString();

    std::uniform_int_distribution<int> dist_type(0, 100);
    bool do_delete = (dist_type(rng) > 70) && (current.length() > 0);

    if (do_delete) {
        std::uniform_int_distribution<size_t> dist_idx(
            0, current.length() - 1
        );

        size_t idx = dist_idx(rng);
        OpID target = seq.localDelete(idx);

        if (target.clock != 0) {
            const auto& delete_ops = seq.getDeleteOperationIds(target);
            assert(!delete_ops.empty());

            OpID delete_operation_id = delete_ops.back();

            network_buffer.push_back({
                user_idx + 1,
                Atom(),
                true,
                target,
                delete_operation_id
            });
        }
    } else {
        std::uniform_int_distribution<size_t> dist_idx(
            0, current.length()
        );
        std::uniform_int_distribution<int> dist_char(65, 90);

        size_t idx = dist_idx(rng);
        char c = static_cast<char>(dist_char(rng));

        Atom new_atom = seq.localInsert(idx, c);

        network_buffer.push_back({
            user_idx + 1,
            new_atom,
            false,
            {0, 0},
            {0, 0}
        });
    }
}

int main(int argc, char** argv) {
    if (argc > 1) OPS_PER_USER = std::atoi(argv[1]);
    if (argc > 2) NUM_USERS = std::atoi(argv[2]);
    if (argc > 3) RANDOM_SEED = std::atoi(argv[3]);

    rng.seed(RANDOM_SEED);

    std::cout << "--- OmniSync Fuzz Test: Chaos Mode ---\n";
    std::cout << "Users: " << NUM_USERS << "\n";
    std::cout << "Ops/User: " << OPS_PER_USER << "\n";
    std::cout << "Total Ops: " << NUM_USERS * OPS_PER_USER << "\n";

    // Initialize users
    users.reserve(NUM_USERS);

    for (int i = 0; i < NUM_USERS; i++) {
        users.push_back(std::make_shared<Sequence>(i + 1));

        users[i]->setOrphanConfig({
            1000000,
            UINT64_MAX
        });
    }

    // Generate local operations
    std::cout << "Generating "
              << NUM_USERS * OPS_PER_USER
              << " operations...\n";

    for (int i = 0; i < OPS_PER_USER; i++) {
        for (int u = 0; u < NUM_USERS; u++) {
            random_op(u);
        }
    }

    // Shuffle packets to simulate extreme network lag
    std::cout << "Shuffling "
              << network_buffer.size()
              << " packets to simulate extreme lag...\n";

    std::shuffle(network_buffer.begin(), network_buffer.end(), rng);

    // Deliver every packet to every other user
    std::cout << "Syncing all users...\n";

    for (int u = 0; u < NUM_USERS; u++) {
        auto& seq = *users[u];

        for (const auto& packet : network_buffer) {
            // Don't deliver a user's own operation back to itself.
            if (packet.from_user == (u + 1)) {
                continue;
            }

            if (packet.is_delete) {
                seq.remoteDelete(
                    packet.delete_target,
                    packet.delete_operation_id
                );
            } else {
                seq.remoteMerge(packet.atom);
            }
        }
    }

    // Verification
    std::cout << "\n--- VERIFICATION ---\n";

    std::string golden = users[0]->toString();
    bool all_match = true;

    for (int u = 1; u < NUM_USERS; u++) {
        std::string check = users[u]->toString();

        if (check != golden) {
            all_match = false;

            std::cout << "MISMATCH found!\n";
            std::cout << "User 0 length: "
                      << golden.length() << "\n";
            std::cout << "User " << u << " length: "
                      << check.length() << "\n";

            std::cout << "User 0 snippet: "
                      << golden.substr(0, 50) << "...\n";

            std::cout << "User " << u << " snippet: "
                      << check.substr(0, 50) << "...\n";
        }
    }

    if (all_match) {
        std::cout << "SUCCESS: All "
                  << NUM_USERS
                  << " users converged identically.\n";

        std::cout << "Final Content Length: "
                  << golden.length() << "\n";

        return 0;
    }

    std::cout << "FAILURE: Consistency broken.\n";
    return 1;
}