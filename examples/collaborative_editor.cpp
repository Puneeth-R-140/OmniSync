/**
 * Real-Time Collaborative Rich-Text Terminal Editor
 * 
 * Uses OmniSync CRDT sequence and range-based marks to build a peer-to-peer
 * interactive text editor displaying real-time synchronization and formatting
 * (bold, italic, underline, colors) rendered in the console using ANSI styling.
 * 
 * Author: Puneeth R
 * Date: August 2026
 */

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sstream>
#include <unordered_set>
#include <conio.h> // Windows console input library

#include "omnisync/omnisync.hpp"

using namespace omnisync::core;
using namespace omnisync::network;

// Custom packet serialization
std::vector<uint8_t> serializeSync(const Sequence& doc, const VectorClock& my_clock, const VectorClock& target_peer_clock) {
    std::vector<uint8_t> buffer;
    buffer.push_back(0x01); // Sync packet header
    
    // Serialize Vector Clock of sender
    std::stringstream clock_ss;
    my_clock.save(clock_ss);
    std::string clock_str = clock_ss.str();
    uint32_t clock_len = static_cast<uint32_t>(clock_str.size());
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&clock_len), reinterpret_cast<const uint8_t*>(&clock_len) + 4);
    buffer.insert(buffer.end(), clock_str.begin(), clock_str.end());
    
    // Serialize atoms delta relative to peer's known clock
    std::vector<Atom> delta = doc.getDelta(target_peer_clock);
    uint32_t num_atoms = static_cast<uint32_t>(delta.size());
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&num_atoms), reinterpret_cast<const uint8_t*>(&num_atoms) + 4);
    for (const auto& atom : delta) {
        std::vector<uint8_t> packed = VLEPacker::pack(atom);
        uint32_t pack_len = static_cast<uint32_t>(packed.size());
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&pack_len), reinterpret_cast<const uint8_t*>(&pack_len) + 4);
        buffer.insert(buffer.end(), packed.begin(), packed.end());
    }
    
    // Serialize marks
    auto all_marks = doc.getAllMarks();
    uint32_t num_marks = static_cast<uint32_t>(all_marks.size());
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&num_marks), reinterpret_cast<const uint8_t*>(&num_marks) + 4);
    for (const auto& mark : all_marks) {
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&mark.id.client_id), reinterpret_cast<const uint8_t*>(&mark.id.client_id) + 8);
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&mark.id.clock), reinterpret_cast<const uint8_t*>(&mark.id.clock) + 8);
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&mark.start_id.client_id), reinterpret_cast<const uint8_t*>(&mark.start_id.client_id) + 8);
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&mark.start_id.clock), reinterpret_cast<const uint8_t*>(&mark.start_id.clock) + 8);
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&mark.end_id.client_id), reinterpret_cast<const uint8_t*>(&mark.end_id.client_id) + 8);
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&mark.end_id.clock), reinterpret_cast<const uint8_t*>(&mark.end_id.clock) + 8);
        
        uint8_t del = mark.is_deleted ? 1 : 0;
        buffer.push_back(del);
        
        uint32_t type_len = static_cast<uint32_t>(mark.type.size());
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&type_len), reinterpret_cast<const uint8_t*>(&type_len) + 4);
        buffer.insert(buffer.end(), mark.type.begin(), mark.type.end());
    }
    
    return buffer;
}

bool deserializeSync(const std::vector<uint8_t>& buffer, Sequence& doc, VectorClock& out_peer_clock) {
    if (buffer.empty() || buffer[0] != 0x01) return false;
    
    size_t offset = 1;
    
    // Read Vector Clock
    if (offset + 4 > buffer.size()) return false;
    uint32_t clock_len = *reinterpret_cast<const uint32_t*>(&buffer[offset]);
    offset += 4;
    
    if (offset + clock_len > buffer.size()) return false;
    std::string clock_str(buffer.begin() + offset, buffer.begin() + offset + clock_len);
    offset += clock_len;
    
    std::stringstream clock_ss(clock_str);
    out_peer_clock.load(clock_ss);
    
    // Read atoms
    if (offset + 4 > buffer.size()) return false;
    uint32_t num_atoms = *reinterpret_cast<const uint32_t*>(&buffer[offset]);
    offset += 4;
    
    std::vector<Atom> delta;
    for (uint32_t i = 0; i < num_atoms; i++) {
        if (offset + 4 > buffer.size()) return false;
        uint32_t pack_len = *reinterpret_cast<const uint32_t*>(&buffer[offset]);
        offset += 4;
        
        if (offset + pack_len > buffer.size()) return false;
        std::vector<uint8_t> packed(buffer.begin() + offset, buffer.begin() + offset + pack_len);
        offset += pack_len;
        
        Atom atom;
        if (!VLEPacker::unpack(packed, atom)) return false;
        delta.push_back(atom);
    }
    
    // Apply atoms and merge clock
    doc.applyDelta(delta);
    doc.mergeVectorClock(out_peer_clock);
    
    // Read marks
    if (offset + 4 > buffer.size()) return false;
    uint32_t num_marks = *reinterpret_cast<const uint32_t*>(&buffer[offset]);
    offset += 4;
    
    for (uint32_t i = 0; i < num_marks; i++) {
        if (offset + 8 * 6 + 1 + 4 > buffer.size()) return false;
        Sequence::Mark mark;
        mark.id.client_id = *reinterpret_cast<const uint64_t*>(&buffer[offset]); offset += 8;
        mark.id.clock = *reinterpret_cast<const uint64_t*>(&buffer[offset]); offset += 8;
        mark.start_id.client_id = *reinterpret_cast<const uint64_t*>(&buffer[offset]); offset += 8;
        mark.start_id.clock = *reinterpret_cast<const uint64_t*>(&buffer[offset]); offset += 8;
        mark.end_id.client_id = *reinterpret_cast<const uint64_t*>(&buffer[offset]); offset += 8;
        mark.end_id.clock = *reinterpret_cast<const uint64_t*>(&buffer[offset]); offset += 8;
        
        mark.is_deleted = (buffer[offset++] == 1);
        
        uint32_t type_len = *reinterpret_cast<const uint32_t*>(&buffer[offset]); offset += 4;
        if (offset + type_len > buffer.size()) return false;
        mark.type.assign(buffer.begin() + offset, buffer.begin() + offset + type_len);
        offset += type_len;
        
        doc.remoteMergeMark(mark);
    }
    
    return true;
}

// Clean screen rendering
void render(const Sequence& doc, size_t cursor_pos, bool command_mode, const std::string& command_buffer, 
            uint64_t client_id, int local_port, const std::string& peer_ip, int peer_port) {
    // Clear screen terminal command
    std::cout << "\x1b[H\x1b[J";
    
    // Styled Header
    std::cout << "\x1b[45m\x1b[37m ========================================== \x1b[0m\n";
    std::cout << "\x1b[45m\x1b[37m  OmniSync Collaborative Rich Text Editor   \x1b[0m\n";
    std::cout << "\x1b[45m\x1b[37m  Client: " << client_id << " | Port: " << local_port << " | Peer: " << peer_ip << ":" << peer_port << " \x1b[0m\n";
    std::cout << "\x1b[45m\x1b[37m ========================================== \x1b[0m\n\n";
    
    // Styled Document
    auto chars = doc.getStyledCharacters();
    std::cout << "Doc: ";
    for (size_t i = 0; i <= chars.size(); ++i) {
        if (i == cursor_pos) {
            std::cout << "\x1b[7m"; // Visual cursor block (reverse video)
        }
        
        if (i < chars.size()) {
            auto& p = chars[i];
            std::string style_codes = "";
            for (const auto& s : p.second) {
                if (s == "bold") style_codes += "\x1b[1m";
                else if (s == "italic") style_codes += "\x1b[3m";
                else if (s == "underline") style_codes += "\x1b[4m";
                else if (s == "red") style_codes += "\x1b[31m";
                else if (s == "green") style_codes += "\x1b[32m";
                else if (s == "blue") style_codes += "\x1b[34m";
            }
            std::cout << style_codes << p.first << "\x1b[0m";
        } else if (i == cursor_pos) {
            std::cout << " ";
        }
        
        if (i == cursor_pos) {
            std::cout << "\x1b[0m";
        }
    }
    std::cout << "\n\n";
    
    // Help Section
    std::cout << "\x1b[90mCommands: Esc to toggle modes | Left/Right Arrows to move cursor\x1b[0m\n";
    std::cout << "\x1b[90mFormatting: :bold <start> <len> | :italic <start> <len> | :red <start> <len>\x1b[0m\n";
    std::cout << "\x1b[90mOther: :clear | :gc | :q (exit)\x1b[0m\n\n";
    
    // Status Bar
    if (command_mode) {
        std::cout << ":" << command_buffer << "\x1b[K";
    } else {
        std::cout << "-- INSERT MODE --\x1b[K";
    }
    std::cout << std::flush;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: collaborative_editor <client_id> <local_port> <peer_ip> <peer_port>\n";
        return 1;
    }
    
    uint64_t client_id = std::stoull(argv[1]);
    int local_port = std::stoi(argv[2]);
    std::string peer_ip = argv[3];
    int peer_port = std::stoi(argv[4]);
    
    // Initialize OmniSync doc
    Sequence doc(client_id);
    
    // Setup socket
    UdpSocket socket;
    if (!socket.bind(local_port)) {
        std::cerr << "Failed to bind to local port " << local_port << std::endl;
        return 1;
    }
    
    // Track target peer clock
    uint64_t peer_client_id = (client_id == 1) ? 2 : 1;
    VectorClock peer_clock(peer_client_id);
    
    // Enable ANSI escapes in Windows command prompt
    system("cls");
    
    bool running = true;
    bool command_mode = false;
    std::string command_buffer = "";
    OpID cursor_atom_id = {0, 0};
    
    bool needs_render = true;
    bool needs_send = false;
    
    while (running) {
        // Non-blocking socket poll
        std::vector<uint8_t> data;
        std::string ip;
        int port;
        while (socket.receiveFrom(data, ip, port)) {
            VectorClock temp_clock(peer_client_id);
            if (deserializeSync(data, doc, temp_clock)) {
                peer_clock.merge(temp_clock);
                needs_render = true;
            }
        }

        if (needs_send) {
            auto packet = serializeSync(doc, doc.getVectorClock(), peer_clock);
            socket.sendTo(peer_ip, peer_port, packet);
            needs_send = false;
        }
        
        if (needs_render) {
            size_t visual_pos = (cursor_atom_id == OpID{0, 0}) ? 0 : doc.getVisualIndex(cursor_atom_id) + 1;
            render(doc, visual_pos, command_mode, command_buffer, client_id, local_port, peer_ip, peer_port);
            needs_render = false;
        }
        
        if (_kbhit()) {
            int ch = _getch();
            if (command_mode) {
                if (ch == 27) { // Escape - switch mode
                    command_mode = false;
                    command_buffer = "";
                    needs_render = true;
                } else if (ch == 13) { // Enter - execute command
                    std::stringstream ss(command_buffer);
                    std::string cmd;
                    ss >> cmd;
                    
                    if (cmd == "q") {
                        running = false;
                    } else if (cmd == "gc") {
                        doc.garbageCollectLocal(0);
                        needs_render = true;
                        needs_send = true;
                    } else if (cmd == "clear") {
                        auto active_marks = doc.getActiveMarks();
                        for (const auto& m : active_marks) {
                            doc.removeMark(m.id);
                        }
                        needs_render = true;
                        needs_send = true;
                    } else if (cmd == "bold" || cmd == "italic" || cmd == "underline" || 
                               cmd == "red" || cmd == "green" || cmd == "blue") {
                        size_t start = 0, len = 0;
                        if (ss >> start >> len && len > 0) {
                            size_t doc_len = doc.toString().size();
                            if (start < doc_len) {
                                size_t end = start + len - 1;
                                if (end >= doc_len) end = doc_len - 1;
                                
                                OpID start_id = doc.getAtomIdAt(start);
                                OpID end_id = doc.getAtomIdAt(end);
                                if (start_id.clock != 0 && end_id.clock != 0) {
                                    doc.addMark(start_id, end_id, cmd);
                                    needs_render = true;
                                    needs_send = true;
                                }
                            }
                        }
                    }
                    command_mode = false;
                    command_buffer = "";
                    needs_render = true;
                } else if (ch == 8) { // Backspace
                    if (!command_buffer.empty()) {
                        command_buffer.pop_back();
                        needs_render = true;
                    }
                } else if (ch >= 32 && ch <= 126) {
                    command_buffer += (char)ch;
                    needs_render = true;
                }
            } else {
                if (ch == 27) { // Escape - switch mode
                    command_mode = true;
                    needs_render = true;
                } else if (ch == 0 || ch == 224) {
                    int special = _getch();
                    if (special == 75) { // Left arrow
                        cursor_atom_id = doc.getPredecessor(cursor_atom_id);
                        needs_render = true;
                    } else if (special == 77) { // Right arrow
                        cursor_atom_id = doc.getSuccessor(cursor_atom_id);
                        needs_render = true;
                    }
                } else if (ch == 8) { // Backspace
                    if (cursor_atom_id != OpID{0, 0}) {
                        OpID prev_id = doc.getPredecessor(cursor_atom_id);
                        doc.localDeleteId(cursor_atom_id);
                        cursor_atom_id = prev_id;
                        needs_render = true;
                        needs_send = true;
                    }
                } else if (ch == 13) { // Enter
                    Atom inserted = doc.localInsertAfter(cursor_atom_id, '\n');
                    cursor_atom_id = inserted.id;
                    needs_render = true;
                    needs_send = true;
                } else if (ch >= 32 && ch <= 126) {
                    Atom inserted = doc.localInsertAfter(cursor_atom_id, (char)ch);
                    cursor_atom_id = inserted.id;
                    needs_render = true;
                    needs_send = true;
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return 0;
}
