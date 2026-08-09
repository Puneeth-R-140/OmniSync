#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>
#include "omnisync/omnisync.hpp"
using namespace omnisync::core;

static void test_adversarial_replay() {
    constexpr int N = 5;
    constexpr int OPS_PER_REPLICA = 160;
    std::mt19937_64 rng(0xC0FFEE123456789ULL);
    std::vector<Sequence> docs;
    docs.reserve(N);
    for (int i=0;i<N;++i) docs.emplace_back(static_cast<uint64_t>(i+1));

    // Each replica performs a mixed workload independently. Deletes are chosen
    // only from locally visible content, while inserts use stable cursor APIs.
    for (int r=0;r<N;++r) {
        for (int k=0;k<OPS_PER_REPLICA;++k) {
            const bool do_delete = docs[r].visibleLength() > 0 && (rng()%100 < 35);
            if (do_delete) {
                const size_t pos = static_cast<size_t>(rng() % docs[r].visibleLength());
                docs[r].localDelete(pos);
            } else {
                const size_t pos = docs[r].visibleLength() == 0
                    ? 0 : static_cast<size_t>(rng() % (docs[r].visibleLength()+1));
                docs[r].localInsert(pos, static_cast<char>('a' + (rng()%26)));
            }
        }
    }

    // Build the final operation set from every replica. Delivery is adversarial:
    // arbitrary order, duplicates, and repeated full replays.
    std::vector<Atom> packets;
    std::unordered_map<OpID, Atom> unique;
    for (const auto& doc : docs) {
        for (const auto& atom : doc.getAtoms()) unique.emplace(atom.id, atom);
    }
    packets.reserve(unique.size());
    for (const auto& [id, atom] : unique) packets.push_back(atom);

    std::shuffle(packets.begin(), packets.end(), rng);
    for (int r=0;r<N;++r) {
        std::vector<Atom> delivery = packets;
        for (size_t i=0;i<packets.size()/4;++i) delivery.push_back(packets[rng()%packets.size()]);
        std::shuffle(delivery.begin(), delivery.end(), rng);
        for (const auto& atom : delivery) docs[r].remoteMerge(atom);
        // Full replay after convergence must be harmless.
        std::shuffle(delivery.begin(), delivery.end(), rng);
        for (const auto& atom : delivery) docs[r].remoteMerge(atom);
    }

    const std::string expected = docs.front().toString();
    for (int r=1;r<N;++r) assert(docs[r].toString() == expected);

    // Every replica should know the contiguous final sequence for every client.
    for (int client=1; client<=N; ++client) {
        uint64_t max_seq=0;
        for (const auto& atom : packets) {
            if (atom.id.client_id==static_cast<uint64_t>(client)) max_seq=std::max(max_seq,atom.id.sequence);
            for (const auto& d : atom.delete_operation_ids)
                if (d.client_id==static_cast<uint64_t>(client)) max_seq=std::max(max_seq,d.sequence);
        }
        for (const auto& doc : docs) assert(doc.getVectorClock().get(client)==max_seq);
    }
}

int main(){ test_adversarial_replay(); std::cout << "chaos_test: PASS\n"; }
