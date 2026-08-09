#pragma once

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstring> 
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include "crdt_atom.hpp"
#include "lamport_clock.hpp"
#include "vector_clock.hpp"
#include "memory_stats.hpp"

namespace omnisync {
namespace core {

struct AVLNode {
    OpID id;
    std::list<Atom>::iterator atom_it;
    size_t weight;          // 1 if active, 0 if deleted/sentinel
    size_t subtree_weight;  // Sum of weights in subtree
    int height;
    AVLNode* left;
    AVLNode* right;
    AVLNode* parent;

    AVLNode(OpID id_, std::list<Atom>::iterator it_, size_t w)
        : id(id_), atom_it(it_), weight(w), subtree_weight(w), height(1),
          left(nullptr), right(nullptr), parent(nullptr) {}
};

/**
 * @brief The RGA Sequence Container (Production Ready).
 * Features:
 * - Deterministic RGA insertion order with a stable list as the source of truth
 * - Orphan Buffering (handles out-of-order parents)
 * - Delete Buffering (handles out-of-order deletes)
 * - Unified Merge Logic (Local == Remote)
 * - Binary Serialization (Save/Load)
 * - Delta Sync using vector-clock state
 */
class Sequence {
public:
    /**
     * @brief Configuration for garbage collection behavior.
     */
    struct GCConfig {
        bool auto_gc_enabled = false;      // Enable automatic GC
        size_t tombstone_threshold = 1000; // Auto-GC trigger point
        uint64_t min_age_threshold = 100;  // Compatibility only; not a GC proof
    };
    
    /**
     * @brief Configuration for orphan buffer management.
     */
    struct OrphanConfig {
        size_t max_orphan_buffer_size = 10000; // Total orphans across all buffers
        uint64_t max_orphan_age = 1000;        // Retained for API compatibility; not used for correctness
        size_t max_delete_buffer_size = 10000; // Unique delete operations waiting for their target
    };

    struct Mark {
        OpID id;           // Unique ID for the mark {client_id, HLC clock, sequence}
        OpID start_id;     // OpID of the character where formatting starts (inclusive)
        OpID end_id;       // OpID of the character where formatting ends (inclusive)
        std::string type;  // e.g., "bold", "italic", "underline", "red", "green", etc.
        bool is_deleted = false;
        OpID deletion_id;
    };

private:
    uint64_t my_client_id;
    LamportClock clock;
    VectorClock vector_clock;  // Track causality for delta sync
    
    // Primary Storage
    std::list<Atom> atoms;
    
    // Order-statistics index over the authoritative RGA list.
    // Rebuilt after materialized insertions to keep list/index identity exact.
    std::unordered_map<OpID, AVLNode*> atom_index;

    // AVL Tree Root
    AVLNode* root = nullptr;

    // Phase 0: Orphan Buffer
    std::unordered_map<OpID, std::vector<Atom>> pending_orphans;
    
    // Phase 0.5: Delete Buffer
    std::unordered_map<OpID, std::vector<OpID>> pending_deletes;
    
    // Formatting Marks
    std::unordered_map<OpID, Mark> marks;
    
    // Garbage Collection State
    GCConfig gc_config;
    size_t tombstone_count = 0;

    // Cached visible text for repeated toString() calls
    mutable std::string cached_string;
    mutable bool cached_string_dirty = true;
    
    // Orphan Buffer State
    OrphanConfig orphan_config;
    size_t total_orphan_count = 0;

    // Per-client contiguous operation sequence. This is distinct from HLC time.
    uint64_t next_sequence_ = 1;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> received_gaps_;
    std::unordered_set<OpID> pending_delete_operation_ids_;
    
    // GC Performance Tracking
    MemoryStats::GCStats gc_stats_;

    static constexpr OpID kNullID{0, 0, 0};
    static constexpr uint8_t kPersistenceVersion = 6;
    static constexpr uint64_t kMaxAtomsOnLoad = 10'000'000;
    static constexpr uint64_t kMaxMarksOnLoad = 1'000'000;
    static constexpr uint64_t kMaxDeleteIdsPerAtomOnLoad = 1'000'000;
    static constexpr uint32_t kMaxMarkTypeLength = 1'000'000;

    static bool isNull(OpID id) noexcept { return id.isNull(); }

    OpID nextLocalOperationId() {
        if (my_client_id == 0) {
            throw std::invalid_argument("client ID 0 is reserved");
        }
        if (next_sequence_ == std::numeric_limits<uint64_t>::max()) {
            throw std::overflow_error("OmniSync operation sequence exhausted");
        }
        const uint64_t sequence = next_sequence_++;
        const uint64_t hlc = clock.tick();
        vector_clock.update(my_client_id, sequence);
        return OpID{my_client_id, hlc, sequence};
    }

    void recordRemoteSequence(const OpID& id) {
        if (id.client_id == 0 || id.sequence == 0) return;
        if (id.client_id == my_client_id && id.sequence >= next_sequence_) {
            next_sequence_ = (id.sequence == std::numeric_limits<uint64_t>::max())
                ? id.sequence : id.sequence + 1;
        }
        const uint64_t current = vector_clock.get(id.client_id);
        if (id.sequence <= current) return;
        auto& gaps = received_gaps_[id.client_id];
        gaps.insert(id.sequence);
        if (current == std::numeric_limits<uint64_t>::max()) return;
        uint64_t next = current + 1;
        while (gaps.erase(next) != 0) {
            vector_clock.update(id.client_id, next);
            if (next == std::numeric_limits<uint64_t>::max()) break;
            ++next;
        }
        if (gaps.empty()) received_gaps_.erase(id.client_id);
    }

    // AVL Tree Helper Methods
    int getHeight(AVLNode* n) const {
        return n ? n->height : 0;
    }

    int getBalance(AVLNode* n) const {
        return n ? getHeight(n->left) - getHeight(n->right) : 0;
    }

    void updateHeightAndWeight(AVLNode* n) {
        if (!n) return;
        n->height = 1 + std::max(getHeight(n->left), getHeight(n->right));
        n->subtree_weight = n->weight + 
                            (n->left ? n->left->subtree_weight : 0) + 
                            (n->right ? n->right->subtree_weight : 0);
    }

    void invalidateStringCache() {
        cached_string_dirty = true;
    }

    void rotateRight(AVLNode* y) {
        AVLNode* x = y->left;
        AVLNode* T2 = x->right;

        x->right = y;
        y->left = T2;

        x->parent = y->parent;
        if (y->parent) {
            if (y->parent->left == y) y->parent->left = x;
            else y->parent->right = x;
        } else {
            root = x;
        }
        y->parent = x;
        if (T2) T2->parent = y;

        updateHeightAndWeight(y);
        updateHeightAndWeight(x);
    }

    void rotateLeft(AVLNode* x) {
        AVLNode* y = x->right;
        AVLNode* T2 = y->left;

        y->left = x;
        x->right = T2;

        y->parent = x->parent;
        if (x->parent) {
            if (x->parent->left == x) x->parent->left = y;
            else x->parent->right = y;
        } else {
            root = y;
        }
        x->parent = y;
        if (T2) T2->parent = x;

        updateHeightAndWeight(x);
        updateHeightAndWeight(y);
    }

    AVLNode* rebalance(AVLNode* n) {
        int balance = getBalance(n);

        if (balance > 1 && getBalance(n->left) >= 0) {
            AVLNode* new_root = n->left;
            rotateRight(n);
            return new_root;
        }

        if (balance > 1 && getBalance(n->left) < 0) {
            AVLNode* new_root = n->left->right;
            rotateLeft(n->left);
            rotateRight(n);
            return new_root;
        }

        if (balance < -1 && getBalance(n->right) <= 0) {
            AVLNode* new_root = n->right;
            rotateLeft(n);
            return new_root;
        }

        if (balance < -1 && getBalance(n->right) > 0) {
            AVLNode* new_root = n->right->left;
            rotateRight(n->right);
            rotateLeft(n);
            return new_root;
        }

        return n;
    }

    void insertNode(AVLNode* parent_node, AVLNode* new_node) {
        if (!parent_node) return;

        if (!parent_node->right) {
            parent_node->right = new_node;
            new_node->parent = parent_node;
        } else {
            AVLNode* curr = parent_node->right;
            while (curr->left) {
                curr = curr->left;
            }
            curr->left = new_node;
            new_node->parent = curr;
        }

        AVLNode* curr = new_node->parent;
        while (curr) {
            updateHeightAndWeight(curr);
            AVLNode* new_curr = rebalance(curr);
            curr = new_curr->parent;
        }
    }

    void deleteNode(AVLNode* z) {
        if (!z) return;

        if (z->left && z->right) {
            AVLNode* s = z->right;
            while (s->left) s = s->left;

            std::swap(z->id, s->id);
            std::swap(z->atom_it, s->atom_it);
            std::swap(z->weight, s->weight);

            atom_index[z->id] = z;
            atom_index[s->id] = s;

            z = s;
        }

        AVLNode* child = z->left ? z->left : z->right;
        AVLNode* parent = z->parent;

        if (child) {
            child->parent = parent;
        }

        if (parent) {
            if (parent->left == z) parent->left = child;
            else parent->right = child;
        } else {
            root = child;
        }

        delete z;

        AVLNode* curr = parent;
        while (curr) {
            updateHeightAndWeight(curr);
            AVLNode* new_curr = rebalance(curr);
            curr = new_curr->parent;
        }
    }

    void updateWeight(AVLNode* node, size_t new_weight) {
        if (!node || node->weight == new_weight) return;
        node->weight = new_weight;
        AVLNode* curr = node;
        while (curr) {
            updateHeightAndWeight(curr);
            curr = curr->parent;
        }
    }

    void destroyTree(AVLNode* node) {
        if (!node) return;
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }

    AVLNode* buildBalancedTree(
        const std::vector<std::list<Atom>::iterator>& ordered,
        std::size_t begin,
        std::size_t end,
        AVLNode* parent) {
        if (begin >= end) return nullptr;
        const std::size_t mid = begin + (end - begin) / 2;
        auto it = ordered[mid];
        const size_t weight = it->is_deleted || it->id.isNull() ? 0 : 1;
        auto* node = new AVLNode(it->id, it, weight);
        node->parent = parent;
        atom_index[it->id] = node;
        node->left = buildBalancedTree(ordered, begin, mid, node);
        node->right = buildBalancedTree(ordered, mid + 1, end, node);
        updateHeightAndWeight(node);
        return node;
    }

    void rebuildTreeFromList() {
        destroyTree(root);
        root = nullptr;
        atom_index.clear();

        std::vector<std::list<Atom>::iterator> ordered;
        ordered.reserve(atoms.size());
        for (auto it = atoms.begin(); it != atoms.end(); ++it) ordered.push_back(it);
        root = buildBalancedTree(ordered, 0, ordered.size(), nullptr);
    }

    AVLNode* findNodeByPrefixWeight(AVLNode* node, size_t target_weight) const {
        if (!node) return nullptr;
        if (target_weight == 0) {
            AVLNode* curr = node;
            while (curr->left) curr = curr->left;
            return curr;
        }

        AVLNode* curr = node;
        size_t remaining = target_weight;
        while (curr) {
            size_t left_weight = curr->left ? curr->left->subtree_weight : 0;
            if (remaining <= left_weight) {
                if (!curr->left) return curr;
                curr = curr->left;
            } else if (remaining <= left_weight + curr->weight) {
                return curr;
            } else {
                remaining -= (left_weight + curr->weight);
                if (!curr->right) return curr;
                curr = curr->right;
            }
        }
        return nullptr;
    }

public:
    Sequence(uint64_t client_id) : my_client_id(client_id), vector_clock(client_id) {
        if (client_id == 0) {
            throw std::invalid_argument("client ID 0 is reserved");
        }
        OpID start_id = kNullID;
        atoms.emplace_back(start_id, start_id, '\0');
        root = new AVLNode(start_id, atoms.begin(), 0);
        atom_index[start_id] = root;
    }

    ~Sequence() {
        destroyTree(root);
    }

    Sequence(const Sequence&) = delete;
    Sequence& operator=(const Sequence&) = delete;

    Sequence(Sequence&& other) noexcept 
        : my_client_id(other.my_client_id),
          clock(std::move(other.clock)),
          vector_clock(std::move(other.vector_clock)),
          atoms(std::move(other.atoms)),
          atom_index(std::move(other.atom_index)),
          root(other.root),
          pending_orphans(std::move(other.pending_orphans)),
          pending_deletes(std::move(other.pending_deletes)),
          marks(std::move(other.marks)),
          gc_config(other.gc_config),
          tombstone_count(other.tombstone_count),
                    cached_string(std::move(other.cached_string)),
                    cached_string_dirty(other.cached_string_dirty),
          orphan_config(other.orphan_config),
          total_orphan_count(other.total_orphan_count),
          next_sequence_(other.next_sequence_),
          received_gaps_(std::move(other.received_gaps_)),
          pending_delete_operation_ids_(std::move(other.pending_delete_operation_ids_)),
          gc_stats_(other.gc_stats_) {
        other.root = nullptr;
        other.next_sequence_ = 1;
        other.received_gaps_.clear();
        other.pending_delete_operation_ids_.clear();
    }

    Sequence& operator=(Sequence&& other) noexcept {
        if (this != &other) {
            destroyTree(root);
            my_client_id = other.my_client_id;
            clock = std::move(other.clock);
            vector_clock = std::move(other.vector_clock);
            atoms = std::move(other.atoms);
            atom_index = std::move(other.atom_index);
            pending_orphans = std::move(other.pending_orphans);
            pending_deletes = std::move(other.pending_deletes);
            marks = std::move(other.marks);
            gc_config = other.gc_config;
            tombstone_count = other.tombstone_count;
            cached_string = std::move(other.cached_string);
            cached_string_dirty = other.cached_string_dirty;
            orphan_config = other.orphan_config;
            total_orphan_count = other.total_orphan_count;
            next_sequence_ = other.next_sequence_;
            received_gaps_ = std::move(other.received_gaps_);
            pending_delete_operation_ids_ = std::move(other.pending_delete_operation_ids_);
            gc_stats_ = other.gc_stats_;
            root = other.root;
            other.root = nullptr;
            other.next_sequence_ = 1;
            other.received_gaps_.clear();
            other.pending_delete_operation_ids_.clear();
        }
        return *this;
    }

    /*
     * Return true when atom_id is inside the RGA subtree rooted at
     * ancestor_id. RGA stores the sequence in depth-first order, so
     * this lets us skip an entire sibling subtree deterministically.
     */
    bool isInSubtree(OpID atom_id, OpID ancestor_id) const {
        if (atom_id == ancestor_id) {
            return true;
        }

        auto atom_it = atom_index.find(atom_id);
        if (atom_it == atom_index.end()) {
            return false;
        }

        OpID current = atom_it->second->atom_it->origin;

        while (!isNull(current)) {
            if (current == ancestor_id) {
                return true;
            }

            auto parent_it = atom_index.find(current);
            if (parent_it == atom_index.end()) {
                return false;
            }

            current = parent_it->second->atom_it->origin;
        }

        return isNull(ancestor_id);
    }

    /*
     * Find the deterministic RGA insertion position.
     *
     * Direct children of the same parent are ordered by OpID. OpID
     * compares the hybrid logical timestamp first and ClientID second.
     * Therefore edits created at different real times retain that ordering,
     * while simultaneous timestamp collisions still converge deterministically.
     * The complete subtree of an earlier sibling stays before the next sibling.
     */
    std::list<Atom>::iterator findInsertionPosition(
        OpID parent_id,
        OpID new_id)
    {
        auto parent_it = atom_index.find(parent_id);

        if (parent_it == atom_index.end()) {
            return atoms.end();
        }

        auto current =
            std::next(parent_it->second->atom_it);

        while (current != atoms.end()) {
            const Atom& candidate = *current;

            if (candidate.origin == parent_id) {
                if (new_id < candidate.id) {
                    return current;
                }

                /*
                 * Existing sibling sorts before the new sibling.
                 * Skip the entire existing sibling subtree.
                 */
                OpID sibling_id = candidate.id;
                ++current;

                while (
                    current != atoms.end() &&
                    isInSubtree(current->id, sibling_id))
                {
                    ++current;
                }

                continue;
            }

            /*
             * Still inside the parent's subtree. Continue until the
             * next unrelated branch.
             */
            if (isInSubtree(candidate.id, parent_id)) {
                ++current;
                continue;
            }

            break;
        }

        return current;
    }

    void applyDeleteToExisting(OpID target_id, OpID delete_operation_id) {
        auto it = atom_index.find(target_id);
        if (it == atom_index.end()) return;

        auto& atom = it->second->atom_it;
        if (!isNull(delete_operation_id)) {
            if (std::find(atom->delete_operation_ids.begin(),
                          atom->delete_operation_ids.end(),
                          delete_operation_id) == atom->delete_operation_ids.end()) {
                atom->delete_operation_ids.push_back(delete_operation_id);
            }
            pending_delete_operation_ids_.erase(delete_operation_id);
        }

        if (!atom->is_deleted) {
            atom->is_deleted = true;
            ++tombstone_count;
            updateWeight(it->second, 0);
            invalidateStringCache();
        }
    }

    void applyPendingDeletes(OpID atom_id) {
        auto pending = pending_deletes.find(atom_id);
        if (pending == pending_deletes.end()) return;

        auto operations = std::move(pending->second);
        pending_deletes.erase(pending);
        for (const OpID& delete_id : operations) {
            clock.merge(delete_id.clock);
            recordRemoteSequence(delete_id);
            applyDeleteToExisting(atom_id, delete_id);
        }
    }

    /*
     * Backwards-compatible API. The integer is treated as a visual
     * insertion cursor and immediately converted to a stable OpID.
     */
    Atom localInsert(size_t literal_index, char content) {
        return localInsertAtCursor(literal_index, content);
    }

    /*
     * Stable cursor insertion. The cursor is represented by the OpID
     * of the character immediately before it.
     */
    Atom localInsertAtCursor(size_t cursor_index, char content) {
        OpID parent_id = getInsertAnchorAt(cursor_index);
        return localInsertAfter(parent_id, content);
    }

    void remoteMerge(Atom new_atom) {
        // HLC time orders operations; the per-client sequence number carries causality.
        clock.merge(new_atom.id.clock);

        if (new_atom.id.isNull()) return;
        if (atom_index.count(new_atom.id)) {
            for (const OpID& delete_id : new_atom.delete_operation_ids) {
                remoteDelete(new_atom.id, delete_id);
            }
            if (new_atom.is_deleted && new_atom.delete_operation_ids.empty()) {
                remoteDelete(new_atom.id);
            }
            return;
        }

        for (const OpID& delete_id : new_atom.delete_operation_ids) {
            if (!delete_id.isNull()) clock.merge(delete_id.clock);
        }

        auto parent_map_it = atom_index.find(new_atom.origin);
        if (parent_map_it == atom_index.end()) {
            if (orphan_config.max_orphan_buffer_size == 0) {
                // Do not advance the vector clock. The sender can retransmit.
                return;
            }
            if (total_orphan_count >= orphan_config.max_orphan_buffer_size) {
                evictOldOrphans();
            }
            if (total_orphan_count >= orphan_config.max_orphan_buffer_size) return;
            pending_orphans[new_atom.origin].push_back(std::move(new_atom));
            ++total_orphan_count;
            for (const OpID& delete_id : pending_orphans[new_atom.origin].back().delete_operation_ids) {
                if (!delete_id.isNull()) pending_delete_operation_ids_.insert(delete_id);
            }
            return;
        }

        // The list is the authoritative RGA order.  The AVL structure is an
        // order-statistics index over that order, so rebuilding it after a
        // materialized insertion is deliberately simpler and safer than
        // mutating a second topology with insertion-position-specific AVL
        // rotations.  This avoids the old class of bugs where list iterators,
        // atom IDs and AVL nodes could disagree after rotations.
        auto current_it = findInsertionPosition(new_atom.origin, new_atom.id);
        auto new_it = atoms.insert(current_it, std::move(new_atom));

        if (new_it->is_deleted) {
            ++tombstone_count;
        }

        rebuildTreeFromList();

        // An insertion is acknowledged only after it is materialized.
        recordRemoteSequence(new_it->id);

        const auto delete_ids = new_it->delete_operation_ids;
        for (const OpID& delete_id : delete_ids) {
            if (!isNull(delete_id)) {
                recordRemoteSequence(delete_id);
                applyDeleteToExisting(new_it->id, delete_id);
            }
        }
        applyPendingDeletes(new_it->id);
        checkPendingOrphans(new_it->id);
        invalidateStringCache();
    }
    
    OpID localDelete(size_t literal_index) {
        AVLNode* target_node = findNodeByPrefixWeight(root, literal_index + 1);
        if (target_node && target_node->weight == 1) {
            const OpID delete_operation_id = nextLocalOperationId();
            const OpID deleted_id = target_node->atom_it->id;
            remoteDelete(deleted_id, delete_operation_id);
            return deleted_id;
        }
        return kNullID;
    }

    void localDeleteId(OpID target_id) {
        if (isNull(target_id)) return;
        const OpID delete_operation_id = nextLocalOperationId();
        remoteDelete(target_id, delete_operation_id);
    }

    void remoteDelete(OpID target_id) {
        // Legacy anonymous deletion is supported for compatibility, but it is
        // deliberately never considered causally stable for GC.
        remoteDelete(target_id, kNullID);
    }

    void remoteDelete(OpID target_id, OpID delete_operation_id) {
        if (isNull(target_id)) return;

        const bool has_operation = !isNull(delete_operation_id);
        if (has_operation) clock.merge(delete_operation_id.clock);

        auto map_it = atom_index.find(target_id);
        if (map_it != atom_index.end()) {
            AVLNode* node = map_it->second;
            if (has_operation) {
                auto& operations = node->atom_it->delete_operation_ids;
                if (std::find(operations.begin(), operations.end(), delete_operation_id) == operations.end()) {
                    operations.push_back(delete_operation_id);
                }
                pending_delete_operation_ids_.erase(delete_operation_id);
                recordRemoteSequence(delete_operation_id);
            }
            if (!node->atom_it->is_deleted) {
                node->atom_it->is_deleted = true;
                ++tombstone_count;
                updateWeight(node, 0);
                invalidateStringCache();
            }
            return;
        }

        if (!has_operation) return;
        if (orphan_config.max_delete_buffer_size == 0 ||
            pending_delete_operation_ids_.size() >= orphan_config.max_delete_buffer_size) {
            // Do not acknowledge a dropped delete. The sender can retransmit.
            return;
        }
        auto& operations = pending_deletes[target_id];
        if (std::find(operations.begin(), operations.end(), delete_operation_id) == operations.end()) {
            operations.push_back(delete_operation_id);
            pending_delete_operation_ids_.insert(delete_operation_id);
        }
    }

    /**
     * @brief DELTA SYNC: Get operations that peer is missing.
     * @param peer_state The vector clock representing what the peer has seen.
     * @return Vector of atoms that are new to the peer.
     * 
     * Example:
     *   My state: {A:5, B:3}
     *   Peer state: {A:3, B:3}
     *   Delta: All operations from A with clock > 3
     */
    std::vector<Atom> getDelta(const VectorClock& peer_state) const {
        std::vector<Atom> delta;
        for (const auto& atom : atoms) {
            if (isNull(atom.id)) continue;

            if (atom.id.sequence > peer_state.get(atom.id.client_id)) {
                delta.push_back(atom);
                continue;
            }

            for (const auto& delete_id : atom.delete_operation_ids) {
                if (delete_id.sequence > peer_state.get(delete_id.client_id)) {
                    delta.push_back(atom);
                    break;
                }
            }

            // Legacy anonymous tombstones have no causal operation ID. They
            // cannot be represented by a vector-clock delta, so keep them
            // retransmittable rather than pretending they are acknowledged.
            if (atom.is_deleted && atom.delete_operation_ids.empty()) {
                delta.push_back(atom);
            }
        }
        return delta;
    }

    /**
     * @brief DELTA SYNC: Apply a delta from another peer.
     * @param delta Vector of atoms received from peer.
     * 
     * This is more efficient than sending the entire document.
     * Instead of sending 10,000 atoms, we might only send 5.
     */
    void applyDelta(const std::vector<Atom>& delta) {
        for (const auto& atom : delta) {
            remoteMerge(atom);
            for (const auto& delete_id : atom.delete_operation_ids) {
                remoteDelete(atom.id, delete_id);
            }
            if (atom.is_deleted && atom.delete_operation_ids.empty()) {
                remoteDelete(atom.id);
            }
        }
    }

    /**
     * @brief Get the current vector clock state.
     * Peers use this to determine what they're missing.
     */
    const VectorClock& getVectorClock() const {
        return vector_clock;
    }

    /**
     * @brief Get the current Lamport logical time without modifying it.
     */
    uint64_t getLamportClock() const {
        return clock.peek();
    }

    /**
     * @brief Merge peer's vector clock (for tracking what they've seen).
     */
    /**
     * @deprecated A peer acknowledgement is not proof that this replica has
     * received those operations. The receive path advances vector_clock when
     * operations are actually materialized. Kept as a no-op for compatibility.
     */
    void mergeVectorClock(const VectorClock& /*peer_clock*/) {}

    /**
     * @brief Perform garbage collection using stable frontier from multiple peers.
     * @param stable_frontier Minimum vector clock across all known peers
     * @return Number of tombstones removed
     * 
     * This is the safe way to GC in a multi-user scenario. The stable frontier
     * represents what ALL peers have seen, so any tombstone before this point
     * can be safely deleted without breaking convergence.
     */
    size_t garbageCollect(const VectorClock& stable_frontier) {
        auto start = std::chrono::steady_clock::now();
        (void)stable_frontier;

        /*
         * IMPORTANT: causal stability is necessary but not sufficient for
         * physical RGA tombstone removal. An old tombstone can remain a valid
         * insertion anchor for a future operation. This implementation does
         * not yet have an explicit anchor-retirement/compaction protocol, so
         * physically deleting the atom would make a later operation with that
         * origin unrecoverable.
         *
         * Therefore the safe behavior is to retain tombstones. The coordinator
         * may still compute and report a stable frontier, but this layer does
         * not turn that frontier into destructive compaction.
         */
        const auto end = std::chrono::steady_clock::now();
        const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        gc_stats_.recordGCRun(static_cast<std::uint64_t>(duration_us), 0);
        return 0;
    }

    /**
     * @brief Conservative local GC compatibility API.
     *
     * Wall-clock/HLC age alone cannot prove that every replica has observed a
     * deletion, nor that the tombstone will never be used as a future RGA
     * anchor. The old implementation therefore made an unsafe assumption.
     * This method intentionally performs no destructive operation.
     */
    size_t garbageCollectLocal(uint64_t /*min_age_threshold*/) {
        return 0;
    }
    
    /**
     * @brief Configure garbage collection behavior.
     */
    void setGCConfig(const GCConfig& config) {
        gc_config = config;
    }
    
    /**
     * @brief Get current GC configuration.
     */
    const GCConfig& getGCConfig() const {
        return gc_config;
    }
    
    /**
     * @brief Configure orphan buffer management.
     */
    void setOrphanConfig(const OrphanConfig& config) {
        orphan_config = config;
    }
    
    /**
     * @brief Get current orphan configuration.
     */
    const OrphanConfig& getOrphanConfig() const {
        return orphan_config;
    }
    
    /**
     * @brief Get current memory usage statistics.
     */
    MemoryStats getMemoryStats() const {
        MemoryStats stats;
        
        stats.atom_count = atoms.size();
        stats.tombstone_count = tombstone_count;
        stats.orphan_count = total_orphan_count;
        stats.delete_buffer_count = pending_deletes.size();
        
        // Approximate memory calculations
        stats.atom_list_bytes = atoms.size() * sizeof(Atom);
        stats.index_map_bytes = atom_index.size() * (sizeof(OpID) + sizeof(AVLNode*) + 32) + atom_index.size() * sizeof(AVLNode); // Map + AVL nodes overhead
        stats.orphan_buffer_bytes = total_orphan_count * sizeof(Atom);
        stats.vector_clock_bytes = vector_clock.getState().size() * 16;
        
        // Copy GC performance stats
        stats.gc_stats = gc_stats_;
        
        return stats;
    }
    
    /**
     * @brief Get total tombstone count.
     */
    size_t getTombstoneCount() const {
        return tombstone_count;
    }
    
    /**
     * @brief Get total orphan buffer size.
     */
    size_t getOrphanBufferSize() const {
        return total_orphan_count;
    }


private:
    void checkPendingOrphans(OpID just_inserted_id) {
        if (pending_orphans.count(just_inserted_id)) {
            std::vector<Atom> children = pending_orphans[just_inserted_id];
            pending_orphans.erase(just_inserted_id);
            total_orphan_count -= children.size();
            for(const auto& child : children) {
                remoteMerge(child);
            }
        }
    }
    
    /**
     * @brief Actually remove tombstones from all data structures.
     */
    void removeTombstones(const std::vector<OpID>& to_remove) {
        if (to_remove.empty()) return;

        for (const auto& id : to_remove) {
            auto map_it = atom_index.find(id);
            if (map_it != atom_index.end()) {
                AVLNode* node = map_it->second;
                auto list_it = node->atom_it;

                // Adjust formatting mark boundaries pointing to this atom
                for (auto& [mark_id, mark] : marks) {
                    if (mark.start_id == id) {
                        auto next_it = std::next(list_it);
                        if (next_it != atoms.end()) {
                            mark.start_id = next_it->id;
                        } else {
                            if (list_it != atoms.begin()) {
                                mark.start_id = std::prev(list_it)->id;
                            } else {
                                mark.start_id = {0, 0};
                            }
                        }
                    }
                    if (mark.end_id == id) {
                        if (list_it != atoms.begin()) {
                            mark.end_id = std::prev(list_it)->id;
                        } else {
                            auto next_it = std::next(list_it);
                            if (next_it != atoms.end()) {
                                mark.end_id = next_it->id;
                            } else {
                                mark.end_id = {0, 0};
                            }
                        }
                    }
                }

                atoms.erase(list_it);
                
                deleteNode(node);
                atom_index.erase(map_it);
                tombstone_count--;
            }
        }

        invalidateStringCache();
    }
    
    /**
     * @brief Determine if an orphan should be accepted into the buffer.
     */
    bool shouldAcceptOrphan(const Atom& /*atom*/) const {
        // Age-based rejection is unsafe: HLC time is not proof that an
        // operation can never arrive later. Capacity is enforced separately.
        return true;
    }

    /**
     * @brief Evict old orphans when the bounded buffer is full.
     *
     * Evicted operations are deliberately not acknowledged in the vector
     * clock. They remain eligible for retransmission from the sender.
     */
    void evictOldOrphans() {
        if (pending_orphans.empty()) return;

        auto victim = pending_orphans.begin();
        for (auto it = std::next(pending_orphans.begin());
             it != pending_orphans.end(); ++it) {
            if (it->second.empty()) continue;
            if (victim->second.empty() || it->second.front().id < victim->second.front().id) {
                victim = it;
            }
        }

        if (!victim->second.empty()) {
            victim->second.erase(victim->second.begin());
            --total_orphan_count;
            if (victim->second.empty()) pending_orphans.erase(victim);
        }
    }

public:
    std::string toString() const {
        if (!cached_string_dirty) {
            return cached_string;
        }

        cached_string.clear();
        for (const auto& a : atoms) {
            if (!a.is_deleted && a.content != 0) {
                cached_string += a.content;
            }
        }
        cached_string_dirty = false;
        return cached_string;
    }

    std::vector<std::pair<char, std::vector<std::string>>> getStyledCharacters() const {
        std::vector<std::pair<char, std::vector<std::string>>> result;
        auto active_marks = getActiveMarks();
        std::unordered_map<OpID, std::vector<std::string>> atom_styles;
        
        for (const auto& mark : active_marks) {
            bool inside = false;
            for (const auto& atom : atoms) {
                if (atom.id == mark.start_id) {
                    inside = true;
                }
                if (inside && !atom.is_deleted && atom.content != '\0') {
                    atom_styles[atom.id].push_back(mark.type);
                }
                if (atom.id == mark.end_id) {
                    inside = false;
                }
            }
        }
        
        for (const auto& atom : atoms) {
            if (!atom.is_deleted && atom.content != '\0') {
                std::vector<std::pair<char, std::vector<std::string>>>::value_type::second_type styles = atom_styles[atom.id];
                result.push_back({atom.content, styles});
            }
        }
        return result;
    }

    OpID getAtomIdAt(size_t index) const {
        size_t count = 0;
        for (const auto& atom : atoms) {
            if (!atom.is_deleted && atom.content != '\0') {
                if (count == index) {
                    return atom.id;
                }
                count++;
            }
        }
        return kNullID;
    }



    /*
     * Convert a visual insertion cursor into a stable CRDT anchor.
     *
     * Cursor 0 means "before the first visible character" and therefore
     * uses the sentinel. Cursor N means "after visible character N-1".
     */
    OpID getInsertAnchorAt(size_t cursor_index) const {
        if (cursor_index == 0) {
            return kNullID;
        }

        size_t visible_count = 0;

        for (const auto& atom : atoms) {
            if (atom.is_deleted || atom.content == '\0') {
                continue;
            }

            if (visible_count + 1 == cursor_index) {
                return atom.id;
            }

            ++visible_count;
        }

        /*
         * Clamp an out-of-range cursor to the end of the visible text.
         */
        if (visible_count > 0) {
            return getAtomIdAt(visible_count - 1);
        }

        return kNullID;
    }

    /*
     * Cursor-oriented alias for editor/UI code.
     */
    OpID getCursorAnchor(size_t cursor_index) const {
        return getInsertAnchorAt(cursor_index);
    }

    /*
     * Return true when an OpID is still present in the sequence.
     * This is useful to editor layers that persist a cursor anchor across
     * remote edits. A deleted atom remains a valid anchor until GC removes it.
     */
    bool hasAtom(OpID id) const {
        return atom_index.find(id) != atom_index.end();
    }

    size_t visibleLength() const {
        size_t count = 0;

        for (const auto& atom : atoms) {
            if (!atom.is_deleted && atom.content != '\0') {
                ++count;
            }
        }

        return count;
    }


    std::vector<Atom> getAtoms() const {
    std::vector<Atom> result;
    result.reserve(atoms.size());

    for (const auto& atom : atoms) {
        if (atom.id.isNull()) {
            continue; // skip sentinel
        }

        result.push_back(atom);
    }

    return result;
    }

    const std::vector<OpID>& getDeleteOperationIds(OpID atom_id) const {
    auto it = atom_index.find(atom_id);

    if (it == atom_index.end()) {
        static const std::vector<OpID> empty;
        return empty;
    }

    return it->second->atom_it->delete_operation_ids;
    }

    Atom localInsertAfter(OpID parent_id, char content) {
        if (!hasAtom(parent_id)) {
            // A local caller must provide a currently resolvable anchor.
            // Clamping to the sentinel is deterministic but hides a caller bug.
            if (!isNull(parent_id)) {
                throw std::invalid_argument("localInsertAfter: unknown parent anchor");
            }
        }

        const OpID new_id = nextLocalOperationId();
        Atom new_atom(new_id, parent_id, content);
        remoteMerge(new_atom);
        return new_atom;
    }

    OpID getPredecessor(OpID id) const {
        auto it = atom_index.find(id);
        if (it == atom_index.end()) return kNullID;
        
        auto list_it = it->second->atom_it;
        while (list_it != atoms.begin()) {
            list_it = std::prev(list_it);
            if (!list_it->is_deleted) {
                return list_it->id;
            }
        }
        return kNullID;
    }

    OpID getSuccessor(OpID id) const {
        auto it = atom_index.find(id);
        if (it == atom_index.end()) {
            auto list_it = atoms.begin();
            while (list_it != atoms.end()) {
                if (list_it != atoms.begin() && !list_it->is_deleted) {
                    return list_it->id;
                }
                list_it = std::next(list_it);
            }
            return kNullID;
        }
        
        auto list_it = std::next(it->second->atom_it);
        while (list_it != atoms.end()) {
            if (!list_it->is_deleted) {
                return list_it->id;
            }
            list_it = std::next(list_it);
        }
        return kNullID;
    }

    size_t getVisualIndex(OpID id) const {
        size_t index = 0;
        for (const auto& atom : atoms) {
            if (atom.id == id) {
                return index;
            }
            if (!atom.is_deleted && atom.content != '\0') {
                index++;
            }
        }
        return 0;
    }

    void addMark(OpID start_id, OpID end_id, const std::string& type) {
        if (!hasAtom(start_id) || !hasAtom(end_id) || type.empty()) return;
        const OpID mark_id = nextLocalOperationId();
        Mark new_mark{mark_id, start_id, end_id, type, false, kNullID};
        marks[mark_id] = std::move(new_mark);
    }
    
    void remoteMergeMark(const Mark& mark) {
        if (mark.id.isNull()) return;
        clock.merge(mark.id.clock);
        recordRemoteSequence(mark.id);
        if (mark.is_deleted && !mark.deletion_id.isNull()) {
            clock.merge(mark.deletion_id.clock);
            recordRemoteSequence(mark.deletion_id);
        }

        auto it = marks.find(mark.id);
        if (it == marks.end()) {
            marks[mark.id] = mark;
        } else if (mark.is_deleted) {
            it->second.is_deleted = true;
            it->second.deletion_id = mark.deletion_id;
        }
    }
    
    void removeMark(OpID mark_id) {
        const OpID deletion_id = nextLocalOperationId();
        auto it = marks.find(mark_id);
        if (it != marks.end() && !it->second.is_deleted) {
            it->second.is_deleted = true;
            it->second.deletion_id = deletion_id;
        }
    }
    
    std::vector<Mark> getActiveMarks() const {
        std::vector<Mark> active;
        for (const auto& [id, mark] : marks) {
            if (!mark.is_deleted) {
                active.push_back(mark);
            }
        }
        return active;
    }
    
    std::vector<Mark> getAllMarks() const {
        std::vector<Mark> all;
        for (const auto& [id, mark] : marks) {
            all.push_back(mark);
        }
        return all;
    }

    /**
     * @brief Save the complete CRDT state to a binary stream.
     *
     * Version 6 stores the complete three-field OpID, the vector clock,
     * deletion operation metadata, and mark deletion metadata. The format is
     * intentionally explicit rather than relying on object layout.
     */
    void save(std::ostream& out) const {
        const auto write_u64 = [&out](uint64_t value) {
            out.write(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        const auto write_u8 = [&out](uint8_t value) {
            out.write(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        const auto write_id = [&write_u64](const OpID& id) {
            write_u64(id.client_id);
            write_u64(id.clock);
            write_u64(id.sequence);
        };

        out.write("OMNI", 4);
        write_u8(kPersistenceVersion);
        write_u64(my_client_id);
        write_u64(clock.peek());
        vector_clock.save(out);
        write_u64(next_sequence_);

        write_u64(static_cast<uint64_t>(atoms.size()));
        for (const auto& atom : atoms) {
            write_id(atom.id);
            write_id(atom.origin);
            out.write(&atom.content, sizeof(atom.content));
            write_u8(atom.is_deleted ? 1u : 0u);
            write_u64(static_cast<uint64_t>(atom.delete_operation_ids.size()));
            for (const auto& delete_id : atom.delete_operation_ids) {
                write_id(delete_id);
            }
        }

        write_u64(static_cast<uint64_t>(marks.size()));
        for (const auto& [id, mark] : marks) {
            (void)id;
            write_id(mark.id);
            write_id(mark.start_id);
            write_id(mark.end_id);
            const uint64_t type_len = static_cast<uint64_t>(mark.type.size());
            write_u64(type_len);
            if (type_len != 0) {
                out.write(mark.type.data(), static_cast<std::streamsize>(type_len));
            }
            write_u8(mark.is_deleted ? 1u : 0u);
            write_id(mark.deletion_id);
        }
    }

    /**
     * @brief Load a version-6 CRDT state.
     *
     * Loading is transactional: parsing happens into a temporary Sequence and
     * the current document is replaced only after the entire stream validates.
     */
    bool load(std::istream& in) {
    const auto read_u64 = [&in](uint64_t& value) -> bool {
        in.read(reinterpret_cast<char*>(&value), sizeof(value));
        return static_cast<bool>(in);
    };

    const auto read_u8 = [&in](uint8_t& value) -> bool {
        in.read(reinterpret_cast<char*>(&value), sizeof(value));
        return static_cast<bool>(in);
    };

    const auto read_id = [&read_u64](OpID& id) -> bool {
        return read_u64(id.client_id) &&
               read_u64(id.clock) &&
               read_u64(id.sequence);
    };

    char magic[4]{};
    in.read(magic, sizeof(magic));

    if (!in || std::memcmp(magic, "OMNI", sizeof(magic)) != 0) {
        return false;
    }

    uint8_t version = 0;
    if (!read_u8(version) || version != kPersistenceVersion) {
        return false;
    }

    uint64_t client_id = 0;
    uint64_t clock_value = 0;

    if (!read_u64(client_id) || !read_u64(clock_value)) {
        return false;
    }

    if (client_id == 0) {
        return false;
    }

    /*
     * Load into a temporary Sequence.
     *
     * Nothing in the current object is modified until the complete stream
     * has been validated. This keeps load() transactional on failure.
     */
    Sequence loaded(client_id);
    loaded.clock.merge(clock_value);

    if (!loaded.vector_clock.load(in)) {
        return false;
    }

    if (!read_u64(loaded.next_sequence_) ||
        loaded.next_sequence_ == 0) {
        return false;
    }

    uint64_t atom_count = 0;

    if (!read_u64(atom_count) ||
        atom_count == 0 ||
        atom_count > kMaxAtomsOnLoad) {
        return false;
    }

    std::vector<Atom> parsed_atoms;
    parsed_atoms.reserve(static_cast<std::size_t>(atom_count));

    std::unordered_set<OpID> seen_ids;
    seen_ids.reserve(static_cast<std::size_t>(atom_count));

    /*
     * Reconstructed state.
     *
     * tombstone_count_ is derived from the actual persisted atoms rather
     * than serialized separately. This prevents the in-memory accounting
     * state from becoming inconsistent with the document.
     */
    std::size_t parsed_tombstone_count = 0;

    for (uint64_t i = 0; i < atom_count; ++i) {
        Atom atom;

        if (!read_id(atom.id) ||
            !read_id(atom.origin)) {
            return false;
        }

        // Exactly one sentinel is allowed, and it must be first.
        if (atom.id.isNull() && i != 0) {
            return false;
        }

        if (!seen_ids.insert(atom.id).second) {
            return false;
        }

        if (!in.read(&atom.content, sizeof(atom.content))) {
            return false;
        }

        uint8_t deleted = 0;

        if (!read_u8(deleted) || deleted > 1) {
            return false;
        }

        atom.is_deleted = deleted != 0;

        if (atom.is_deleted) {
            ++parsed_tombstone_count;
        }

        uint64_t delete_count = 0;

        if (!read_u64(delete_count) ||
            delete_count > kMaxDeleteIdsPerAtomOnLoad) {
            return false;
        }

        /*
         * Every persisted OpID contains three uint64_t fields.
         * Reject impossible allocations before reserve().
         */
        constexpr uint64_t kSerializedOpIDBytes = sizeof(uint64_t) * 3;

        const uint64_t max_stream_bytes =
            static_cast<uint64_t>(
                std::numeric_limits<std::streamsize>::max());

        if (delete_count >
            max_stream_bytes / kSerializedOpIDBytes) {
            return false;
        }

        atom.delete_operation_ids.reserve(
            static_cast<std::size_t>(delete_count));

        std::unordered_set<OpID> delete_ids_seen;
        delete_ids_seen.reserve(
            static_cast<std::size_t>(delete_count));

        for (uint64_t j = 0; j < delete_count; ++j) {
            OpID delete_id;

            if (!read_id(delete_id) ||
                delete_id.isNull()) {
                return false;
            }

            if (!delete_ids_seen.insert(delete_id).second) {
                return false;
            }

            atom.delete_operation_ids.push_back(delete_id);
        }

        parsed_atoms.push_back(std::move(atom));
    }

    /*
     * Validate the sentinel.
     */
    if (parsed_atoms.empty() ||
        !parsed_atoms.front().id.isNull() ||
        !parsed_atoms.front().origin.isNull()) {
        return false;
    }

    /*
     * Every non-sentinel atom must reference an atom that exists
     * somewhere in the persisted state.
     */
    for (std::size_t i = 1; i < parsed_atoms.size(); ++i) {
        if (seen_ids.find(parsed_atoms[i].origin) == seen_ids.end()) {
            return false;
        }
    }

    /*
     * Marks.
     */
    uint64_t mark_count = 0;

    if (!read_u64(mark_count) ||
        mark_count > kMaxMarksOnLoad) {
        return false;
    }

    std::unordered_map<OpID, Mark> parsed_marks;
    parsed_marks.reserve(static_cast<std::size_t>(mark_count));

    for (uint64_t i = 0; i < mark_count; ++i) {
        Mark mark;

        if (!read_id(mark.id) ||
            mark.id.isNull()) {
            return false;
        }

        if (!read_id(mark.start_id) ||
            !read_id(mark.end_id)) {
            return false;
        }

        /*
         * Mark anchors must refer to persisted atoms.
         *
         * If the persistence format intentionally allows null anchors,
         * this validation should be relaxed separately rather than silently
         * accepting arbitrary missing IDs.
         */
        if (seen_ids.find(mark.start_id) == seen_ids.end() ||
            seen_ids.find(mark.end_id) == seen_ids.end()) {
            return false;
        }

        uint64_t type_len = 0;

        if (!read_u64(type_len) ||
            type_len > kMaxMarkTypeLength) {
            return false;
        }

        mark.type.resize(static_cast<std::size_t>(type_len));

        if (type_len != 0) {
            in.read(
                mark.type.data(),
                static_cast<std::streamsize>(type_len));

            if (!in) {
                return false;
            }
        }

        uint8_t deleted = 0;

        if (!read_u8(deleted) || deleted > 1) {
            return false;
        }

        mark.is_deleted = deleted != 0;

        if (!read_id(mark.deletion_id)) {
            return false;
        }

        if (mark.is_deleted && mark.deletion_id.isNull()) {
            return false;
        }

        if (!parsed_marks.emplace(mark.id, std::move(mark)).second) {
            return false;
        }
    }

    /*
     * Reject trailing bytes.
     *
     * A valid persistence stream must be consumed completely.
     */
    char trailing = 0;

    if (in.get(trailing)) {
        return false;
    }

    if (!in.eof()) {
        return false;
    }

    /*
     * Reconstruct the list.
     *
     * The persisted list order is authoritative. The AVL index is rebuilt
     * from that order rather than depending on the historical insertion
     * sequence or historical tree rotations.
     */
    for (auto& atom : parsed_atoms) {
        loaded.atoms.push_back(std::move(atom));
    }

    loaded.rebuildTreeFromList();

    /*
     * Restore all derived state.
     */
    loaded.marks = std::move(parsed_marks);

    loaded.tombstone_count = parsed_tombstone_count;

    loaded.cached_string.clear();
    loaded.cached_string_dirty = true;

    /*
     * Make sure the next local operation cannot reuse an existing local
     * sequence number.
     */
    const uint64_t local_vector_sequence =
        loaded.vector_clock.get(client_id);

    if (local_vector_sequence ==
        std::numeric_limits<uint64_t>::max()) {
        loaded.next_sequence_ =
            std::numeric_limits<uint64_t>::max();
    } else {
        loaded.next_sequence_ = std::max(
            loaded.next_sequence_,
            local_vector_sequence + 1);
    }

    /*
     * Recompute the next local sequence from every persisted operation.
     */
    const auto advance_next_sequence =
        [&loaded, client_id](const OpID& id) {
            if (id.client_id != client_id) {
                return;
            }

            if (id.sequence >= loaded.next_sequence_) {
                if (id.sequence ==
                    std::numeric_limits<uint64_t>::max()) {
                    loaded.next_sequence_ =
                        std::numeric_limits<uint64_t>::max();
                } else {
                    loaded.next_sequence_ = id.sequence + 1;
                }
            }
        };

    for (const auto& atom : loaded.atoms) {
        advance_next_sequence(atom.id);

        for (const auto& delete_id :
             atom.delete_operation_ids) {
            advance_next_sequence(delete_id);
        }
    }

    for (const auto& [id, mark] : loaded.marks) {
        (void)id;

        advance_next_sequence(mark.id);
        advance_next_sequence(mark.deletion_id);
    }

    /*
     * Commit only after every part of the persistence stream has passed
     * validation and all derived state has been reconstructed.
     */
    *this = std::move(loaded);

    return true;
    }
};

} // namespace core
} // namespace omnisync
