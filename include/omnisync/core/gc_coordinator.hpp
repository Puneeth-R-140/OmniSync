#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "sequence.hpp"
#include "vector_clock.hpp"

namespace omnisync::core {

/** State remembered for one replication peer. */
struct PeerState {
    uint64_t peer_id = 0;
    VectorClock vector_clock;
    std::chrono::steady_clock::time_point last_seen{};
    bool is_active = false;
    bool is_retired = false;

    explicit PeerState(uint64_t id = 0)
        : peer_id(id), vector_clock(id), last_seen(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Computes a distributed GC frontier.
 *
 * A timeout is deliberately NOT treated as an acknowledgement. An offline
 * peer may reconnect later with an old state, so silently excluding it would
 * make tombstone collection unsafe. A peer leaves the GC quorum only through
 * explicit retirePeer(), after the application has established that the peer
 * will not return to the current document incarnation.
 *
 * Retired peers are never implicitly resurrected by heartbeat/update traffic.
 * Re-registration is an explicit operation and resets the peer's causal state,
 * requiring a fresh synchronization handshake before the peer participates in
 * GC again.
 */
class GCCoordinator {
public:
    struct Config {
        uint64_t heartbeat_interval_ms = 5000;
        uint64_t peer_timeout_ms = 30000; // telemetry only; never a GC proof.
        uint64_t gc_interval_ms = 60000;
        bool auto_gc_enabled = true;
        std::size_t min_peers_for_gc = 1;
    };

private:
    uint64_t my_peer_id_;
    Config config_{};
    std::unordered_map<uint64_t, PeerState> peers_;
    std::chrono::steady_clock::time_point last_gc_time_ = std::chrono::steady_clock::now();
    VectorClock my_vector_clock_;

public:
    explicit GCCoordinator(uint64_t my_peer_id, const Config& config)
        : my_peer_id_(my_peer_id), config_(config), my_vector_clock_(my_peer_id) {
        if (my_peer_id_ == 0) throw std::invalid_argument("peer ID 0 is reserved");
    }

    explicit GCCoordinator(uint64_t my_peer_id) : GCCoordinator(my_peer_id, Config{}) {}

    /** Register a peer for the first time. Retired peers are not resurrected. */
    void registerPeer(uint64_t peer_id) {
        if (peer_id == 0 || peer_id == my_peer_id_) return;
        peers_.try_emplace(peer_id, PeerState(peer_id));
    }

    /**
     * Explicitly reactivate a retired peer after a fresh synchronization
     * handshake. The peer starts inactive and cannot affect GC until its
     * current vector clock is supplied through updatePeerState().
     */
    void reRegisterPeer(uint64_t peer_id) {
        if (peer_id == 0 || peer_id == my_peer_id_) return;
        auto [it, inserted] = peers_.try_emplace(peer_id, PeerState(peer_id));
        if (inserted) return;
        auto& state = it->second;
        if (!state.is_retired) return;
        state.vector_clock = VectorClock(peer_id);
        state.last_seen = std::chrono::steady_clock::now();
        state.is_active = false;
        state.is_retired = false;
    }

    void updatePeerState(uint64_t peer_id, const VectorClock& vc) {
        if (peer_id == 0 || peer_id == my_peer_id_) return;
        auto it = peers_.find(peer_id);
        if (it == peers_.end()) {
            registerPeer(peer_id);
            it = peers_.find(peer_id);
        }
        auto& state = it->second;
        // Receiving traffic from a retired peer must never resurrect it.
        if (state.is_retired) return;
        state.vector_clock = vc;
        state.last_seen = std::chrono::steady_clock::now();
        state.is_active = true;
    }

    void processHeartbeat(uint64_t peer_id, const VectorClock& vc) { updatePeerState(peer_id, vc); }

    void retirePeer(uint64_t peer_id) {
        if (peer_id == my_peer_id_) return;
        auto it = peers_.find(peer_id);
        if (it != peers_.end()) {
            it->second.is_retired = true;
            it->second.is_active = false;
        }
    }

    // Kept for API compatibility. It means explicit retirement, not timeout.
    void removePeer(uint64_t peer_id) { retirePeer(peer_id); }

    std::vector<PeerState> getActivePeers() const {
        std::vector<PeerState> active;
        for (const auto& [_, state] : peers_)
            if (!state.is_retired && state.is_active) active.push_back(state);
        return active;
    }

    std::vector<PeerState> getKnownPeers() const {
        std::vector<PeerState> result;
        for (const auto& [_, state] : peers_)
            if (!state.is_retired) result.push_back(state);
        return result;
    }

    /** Returns true only when every non-retired peer has supplied a state. */
    bool hasCompleteQuorum() const {
        if (getKnownPeers().size() < config_.min_peers_for_gc) return false;
        for (const auto& [_, state] : peers_)
            if (!state.is_retired && !state.is_active) return false;
        return true;
    }

    VectorClock computeStableFrontier() const {
        std::vector<VectorClock> clocks;
        clocks.reserve(peers_.size() + 1);
        clocks.push_back(my_vector_clock_);
        for (const auto& [_, state] : peers_)
            if (!state.is_retired) clocks.push_back(state.vector_clock);
        return VectorClock::computeMinimum(clocks);
    }

    bool shouldTriggerGC() const {
        if (!config_.auto_gc_enabled || !hasCompleteQuorum()) return false;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_gc_time_).count();
        return elapsed >= static_cast<long long>(config_.gc_interval_ms);
    }

    std::size_t performCoordinatedGC(Sequence& doc) {
        if (!hasCompleteQuorum()) return 0;
        const auto frontier = computeStableFrontier();
        const std::size_t removed = doc.garbageCollect(frontier);
        last_gc_time_ = std::chrono::steady_clock::now();
        return removed;
    }

    void updateMyVectorClock(const VectorClock& vc) { my_vector_clock_ = vc; }
    const VectorClock& getMyVectorClock() const noexcept { return my_vector_clock_; }
    const Config& getConfig() const noexcept { return config_; }
    void setConfig(const Config& config) { config_ = config; }
    std::size_t getPeerCount() const noexcept { return peers_.size(); }
    std::size_t getActivePeerCount() const { return getActivePeers().size(); }

    void sendHeartbeat(std::function<void(uint64_t, const VectorClock&)> send_fn) const {
        if (!send_fn) return;
        for (const auto& [peer_id, state] : peers_)
            if (!state.is_retired) send_fn(peer_id, my_vector_clock_);
    }
};

} // namespace omnisync::core
