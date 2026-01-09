#pragma once

#include <list>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include "crdt_atom.hpp"
#include "lamport_clock.hpp"

namespace omnisync {
namespace core {

/**
 * @brief The RGA Sequence Container (Optimized).
 * Uses std::list for stable iterators and O(1) insertions.
 * Uses std::unordered_map for O(1) parent lookup.
 */
class Sequence {
private:
    uint64_t my_client_id;
    LamportClock clock;         
    
    // Primary Storage: Doubly Linked List (Stable Pointers/Iterators)
    std::list<Atom> atoms;
    
    // Optimization Index: Instant lookup of any Atom by ID
    std::unordered_map<OpID, std::list<Atom>::iterator> atom_index;

public:
    Sequence(uint64_t client_id) : my_client_id(client_id) {
        // Initialize Start Node
        OpID start_id = {0, 0};
        atoms.emplace_back(start_id, start_id, 0);
        
        // Index it
        atom_index[start_id] = atoms.begin();
    }

    /**
     * @brief LOCAL INSERT
     */
    Atom localInsert(size_t literal_index, char content) {
        uint64_t tick = clock.tick();
        OpID new_id = { my_client_id, tick };

        // 1. Find Parent by walking the list (View Layer mapping)
        // Since 'literal_index' ignores tombstones, we must scan.
        // (Further optimization: Use a Tree/Rope for this scan. For now O(N) scan is unavoidable for View mapping).
        
        auto it = atoms.begin();
        size_t current_visual_idx = 0;
        
        // Skip START node (it is technically index -1 or hidden)
        // Let's assume literal_index 0 means "After Start".
        
        // Allow inserting *before* everything? No, always after Start.
        // Start node is visual index -1.
        
        // We need to find the atom that is currently displayed at 'literal_index'.
        // If literal_index is 0, we want to insert after the Atom that represents visual position -1 (Start)?
        // Or if literal_index is 0, we insert after atoms[0]?
        
        // Simple mapping: 
        // Iterate. Count non-deleted atoms.
        // Stop when count == literal_index + 1?
        
        // Find the node that will be the PREDECESSOR.
        // If inserting at 0 (start of text), Predecessor is StartNode.
        // If inserting at 1, Predecessor is the 1st visible char.
        
        // Correct loop:
        // We assume StartNode is invisible.
        // If literal_index == 0, we want parent = StartNode.
        
        int steps_needed = (int)literal_index; // 0 means insert after Start
        
        while (it != atoms.end()) {
            if (!it->is_deleted && it->content != 0) {
                 if (steps_needed == 0) break; // Found parent
                 steps_needed--;
            }
            // If we are at StartNode, we act as if we found it instantly if index is 0?
            if (it->content == 0 && steps_needed == 0) break; 
            
            it++;
            if (it == atoms.end()) {
                it--; // Clamp to last element
                break;
            }
        }
        
        // 'it' is now the Parent
        OpID parent_id = it->id;
        Atom new_atom(new_id, parent_id, content);
        
        // 2. Insert After Parent
        // locally, we just append to the right of parent.
        auto new_it = atoms.insert(std::next(it), new_atom);
        
        // 3. Update Index
        atom_index[new_id] = new_it;

        return new_atom;
    }

    /**
     * @brief REMOTE MERGE (Optimized O(1) Lookup)
     */
    void remoteMerge(Atom new_atom) {
        clock.merge(new_atom.id.clock);

        // 1. FAST LOOKUP: Find parent instantly
        auto parent_map_it = atom_index.find(new_atom.origin);
        if (parent_map_it == atom_index.end()) {
            std::cerr << "CRITICAL: Orphan atom received!" << std::endl;
            return;
        }

        auto parent_it = parent_map_it->second;

        // 2. The Skipping Logic (RGA)
        auto current_it = std::next(parent_it);
        
        while (current_it != atoms.end()) {
            const Atom& c = *current_it;
            
            // Subtree Scan Stop
            if (c.origin.clock < new_atom.origin.clock) {
                break;
            }

            // Sibling Check
            if (c.origin == new_atom.origin) {
                if (new_atom.id < c.id) {
                    break; // Insert Here
                }
                // Else Skip
            }
            
            current_it++;
        }

        // 3. Insert Stable Iterator
        auto new_it = atoms.insert(current_it, new_atom);
        atom_index[new_atom.id] = new_it;
    }

    /**
     * @brief LOCAL DELETE
     * Marks an atom as deleted (Tombstone).
     * @param literal_index The visible index to delete.
     * @return The ID of the atom that was targeted.
     */
    OpID localDelete(size_t literal_index) {
        clock.tick(); 

        auto it = atoms.begin();
        size_t current_visual_idx = 0;
        bool found = false;

        while (it != atoms.end()) {
            if (!it->is_deleted && it->content != 0) {
                if (current_visual_idx == literal_index) {
                    found = true;
                    break;
                }
                current_visual_idx++;
            }
            it++;
        }

        if (found) {
            it->is_deleted = true;
            return it->id;
        }
        return {0,0};
    }

    /**
     * @brief REMOTE DELETE
     * Apply a delete operation received from network.
     */
    void remoteDelete(OpID target_id) {
        auto map_it = atom_index.find(target_id);
        if (map_it != atom_index.end()) {
            map_it->second->is_deleted = true;
        }
    }

    /**
     * @brief Debug helper
     */
    std::string toString() const {
        std::string result = "";
        for (const auto& a : atoms) {
            if (!a.is_deleted && a.content != 0) {
                result += a.content;
            }
        }
        return result;
    }
};

} // namespace core
} // namespace omnisync
