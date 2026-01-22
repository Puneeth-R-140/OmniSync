#ifndef OMNISYNC_CORE_GC_COORDINATOR_HPP
#define OMNISYNC_CORE_GC_COORDINATOR_HPP

#include <unordered_map>
#include <vector>
#include <chrono>
#include <functional>
#include "vector_clock.hpp"
#include "sequence.hpp"

namespace omnisync::core {

/**
 * @brief State tracking for a single peer in the distributed system
 */
struct PeerState {
    uint64_t peer_id;
    VectorClock vector_clock;
    std::chrono::steady_clock::time_point last_seen;
    bool is_active;
    
    PeerState(uint64_t id) 
        : peer_id(id)
        , last_seen(std::chrono::steady_clock::now())
        , is_active(false) {}  // Not active until first update
};

/**
 * @brief Coordinates garbage collection across multiple peers
 * 
 * The GCCoordinator tracks vector clocks from all known peers and computes
 * a safe "stable frontier" - the minimum vector clock across all active peers.
 * This frontier represents operations that ALL peers have witnessed, making
 * it safe to delete tombstones before this point.
 * 
 * Usage:
 * ```cpp
 * GCCoordinator gc_coord(my_peer_id);
 * 
 * // Register known peers
 * gc_coord.registerPeer(peer1_id);
 * gc_coord.registerPeer(peer2_id);
 * 
 * // Update their states as you receive operations
 * gc_coord.updatePeerState(peer1_id, their_vector_clock);
 * 
 * // Periodically check if GC should run
 * if (gc_coord.shouldTriggerGC()) {
 *     VectorClock frontier = gc_coord.computeStableFrontier();
 *     size_t removed = doc.garbageCollect(frontier);
 * }
 * ```
 */
class GCCoordinator {
public:
    /**
     * @brief Configuration for GC coordination behavior
     */
    struct Config {
        uint64_t heartbeat_interval_ms = 5000;      // Send heartbeat every 5s
        uint64_t peer_timeout_ms = 30000;           // Peer inactive after 30s
        uint64_t gc_interval_ms = 60000;            // Run GC every 60s
        bool auto_gc_enabled = true;                // Enable automatic GC
        size_t min_peers_for_gc = 1;                // Minimum peers before GC
    };

private:
    uint64_t my_peer_id_;
    Config config_;
    std::unordered_map<uint64_t, PeerState> peers_;
    std::chrono::steady_clock::time_point last_gc_time_;
    VectorClock my_vector_clock_;

public:
    /**
     * @brief Construct a GC coordinator
     * @param my_peer_id This peer's unique identifier
     * @param config Configuration options
     */
    explicit GCCoordinator(uint64_t my_peer_id, const Config& config)
        : my_peer_id_(my_peer_id)
        , config_(config)
        , last_gc_time_(std::chrono::steady_clock::now())
        , my_vector_clock_(my_peer_id) {
    }
    
    /**
     * @brief Construct with default config
     */
    explicit GCCoordinator(uint64_t my_peer_id)
        : GCCoordinator(my_peer_id, Config{}) {
    }

    /**
     * @brief Register a new peer in the system
     */
    void registerPeer(uint64_t peer_id) {
        if (peer_id == my_peer_id_) return;  // Don't register self
        if (peers_.count(peer_id)) return;   // Already registered
        
        peers_.emplace(peer_id, PeerState(peer_id));
    }

    /**
     * @brief Update a peer's vector clock state
     * @param peer_id The peer whose state to update
     * @param vc Their current vector clock
     */
    void updatePeerState(uint64_t peer_id, const VectorClock& vc) {
        auto it = peers_.find(peer_id);
        if (it == peers_.end()) {
            // Auto-register unknown peers
            registerPeer(peer_id);
            it = peers_.find(peer_id);
        }
        
        it->second.vector_clock = vc;
        it->second.last_seen = std::chrono::steady_clock::now();
        it->second.is_active = true;
    }

    /**
     * @brief Mark a peer as disconnected
     */
    void removePeer(uint64_t peer_id) {
        peers_.erase(peer_id);
    }

    /**
     * @brief Get list of currently active peers
     */
    std::vector<PeerState> getActivePeers() const {
        std::vector<PeerState> active;
        auto now = std::chrono::steady_clock::now();
        
        for (const auto& [id, state] : peers_) {
            // Only count as active if:
            // 1. Peer has been updated at least once (is_active)
            // 2. Within timeout window
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state.last_seen).count();
            
            if (state.is_active && elapsed < config_.peer_timeout_ms) {
                active.push_back(state);
            }
        }
        
        return active;
    }

    /**
     * @brief Compute the stable frontier across all active peers
     * 
     * The stable frontier is the minimum vector clock across all peers,
     * representing operations that everyone has seen.
     * 
     * @return VectorClock representing the safe frontier for GC
     */
    VectorClock computeStableFrontier() const {
        std::vector<VectorClock> all_clocks;
        
        // Include all active peer clocks
        for (const auto& peer : getActivePeers()) {
            all_clocks.push_back(peer.vector_clock);
        }
        
        // Include own clock
        all_clocks.push_back(my_vector_clock_);
        
        // If no active peers, return empty clock (no GC)
        if (all_clocks.empty()) {
            return VectorClock(my_peer_id_);
        }
        
        // Compute minimum across all clocks
        return VectorClock::computeMinimum(all_clocks);
    }

    /**
     * @brief Check if GC should be triggered based on time interval
     */
    bool shouldTriggerGC() const {
        if (!config_.auto_gc_enabled) return false;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_gc_time_).count();
        
        // Check time interval
        if (elapsed < config_.gc_interval_ms) return false;
        
        // Check minimum peers requirement
        if (getActivePeers().size() < config_.min_peers_for_gc) return false;
        
        return true;
    }

    /**
     * @brief Perform coordinated GC on a document
     * @param doc The document to garbage collect
     * @return Number of tombstones removed
     */
    size_t performCoordinatedGC(Sequence& doc) {
        VectorClock frontier = computeStableFrontier();
        size_t removed = doc.garbageCollect(frontier);
        
        // Update last GC time
        last_gc_time_ = std::chrono::steady_clock::now();
        
        return removed;
    }

    /**
     * @brief Update own vector clock (call after local operations)
     */
    void updateMyVectorClock(const VectorClock& vc) {
        my_vector_clock_ = vc;
    }

    /**
     * @brief Get current configuration
     */
    const Config& getConfig() const {
        return config_;
    }

    /**
     * @brief Update configuration
     */
    void setConfig(const Config& config) {
        config_ = config;
    }

    /**
     * @brief Get number of registered peers
     */
    size_t getPeerCount() const {
        return peers_.size();
    }

    /**
     * @brief Get number of active peers (within timeout)
     */
    size_t getActivePeerCount() const {
        return getActivePeers().size();
    }

    /**
     * @brief Send heartbeat to all peers
     * 
     * @param send_fn Callback to send vector clock to a peer
     *                Arguments: (peer_id, my_vector_clock)
     */
    void sendHeartbeat(std::function<void(uint64_t, const VectorClock&)> send_fn) {
        for (const auto& [peer_id, state] : peers_) {
            send_fn(peer_id, my_vector_clock_);
        }
    }

    /**
     * @brief Process incoming heartbeat from a peer
     */
    void processHeartbeat(uint64_t peer_id, const VectorClock& vc) {
        updatePeerState(peer_id, vc);
    }
};

} // namespace omnisync::core

#endif // OMNISYNC_CORE_GC_COORDINATOR_HPP
