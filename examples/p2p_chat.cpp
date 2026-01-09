#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

// Windows-specific console input for non-blocking keypress
#ifdef _WIN32
    #include <conio.h>
#else
    // Dummy implementation for Linux (To allow compilation, though behavior will differ)
    int _kbhit() { return 0; }
    int _getch() { return 0; }
#endif

#include "omnisync/omnisync.hpp"
#include "omnisync/network/binary_packer.hpp"
#include "omnisync/network/udp_socket.hpp"

using namespace omnisync::core;
using namespace omnisync::network;

void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Usage: p2p_chat <MyID> <MyPort> <PeerPort>\n";
        std::cout << "Example: p2p_chat 1 8000 8001\n";
        return 1;
    }

    uint64_t my_id = std::stoull(argv[1]);
    int my_port = std::stoi(argv[2]);
    int peer_port = std::stoi(argv[3]);

    std::cout << "--- OmniSync P2P Chat ---\n";
    std::cout << "My ID: " << my_id << " | Port: " << my_port << " -> Peer: " << peer_port << "\n";
    std::cout << "Controls: Type normally. ESC to quit. BACKSPACE to delete.\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 1. Initialize System
    Sequence doc(my_id);
    UdpSocket sock;
    
    if (!sock.bind(my_port)) {
        return 1;
    }

    std::string last_render = "";
    bool running = true;
    size_t cursor_pos = 0; // Simple local cursor tracking

    // Main Loop (Game Loop Style)
    while (running) {
        // --- 1. HANDLE NETWORK ---
        std::vector<uint8_t> packet_data;
        std::string sender_ip;
        int sender_port;

        while (sock.receiveFrom(packet_data, sender_ip, sender_port)) {
            Atom received_atom;
            if (BinaryPacker::unpack(packet_data, received_atom)) {
                if (received_atom.is_deleted) {
                    doc.remoteDelete(received_atom.id);
                } else {
                    doc.remoteMerge(received_atom);
                }
            }
        }

        // --- 2. HANDLE INPUT (Keyboard) ---
        if (_kbhit()) {
            char c = _getch();

            if (c == 27) { // ESC
                running = false;
            } else if (c == 8) { // BACKSPACE
                // Logic: Delete character at cursor-1
                // Current doc.toString() gives actual length.
                // For MVP Chat: Always delete from END (appending mode) to keep visualization simple?
                // Or implement simple left-delete.
                // Let's assume Append-Only cursor for simplicity of console UI, 
                // but we support deleting the last char.
                
                std::string current = doc.toString();
                if (current.length() > 0) {
                     // Delete index length-1
                     OpID target = doc.localDelete(current.length() - 1);
                     
                     // Send Network Packet
                     Atom tombstone; // Dummy atom to carry ID
                     tombstone.id = target;
                     tombstone.is_deleted = true;
                     // We pack this 'tombstone' just to send the ID and is_deleted flag
                     // (Packer handles this: it sends ID + origin + content + flags)
                     // Wait, localDelete returns ID only. To pack it, we need an Atom structure?
                     // Or we create a dedicated Delete Packet?
                     // Re-using Atom structure for delete is fine:
                     // Content=0, ID=TargetID, is_deleted=true.
                     // IMPORTANT: The 'ID' field in delete packet is the TARGET ID to delete.
                     // (Our Packer / Unpack logic needs to respect that).
                     // Actually, Packer packs 'atom.id'.
                     // remoteMerge reads 'atom.id'.
                     // remoteDelete takes 'target_id'.
                     
                     // Let's create a "Delete Request Atom":
                     // It carries the TargetID in its 'id' field? NO.
                     // If we use 'id' field, remoteMerge might try to merge it.
                     
                     // FIX: The current Packer packs an *Atom*. 
                     // When unpacking, we check 'is_deleted'.
                     // If true, we need to know WHICH ID to delete.
                     // Is it the 'atom.id'?
                     // In 'localDelete', we return the ID of the atom *to be deleted*.
                     // So we construct a dummy Atom where atom.id = target_id.
                     // And atom.is_deleted = true.
                     
                     tombstone.id = target; 
                     tombstone.is_deleted = true;
                     // Origin/Content don't matter for delete.
                     
                     auto bytes = BinaryPacker::pack(tombstone);
                     sock.sendTo("127.0.0.1", peer_port, bytes);
                }
            } else {
                // Type Character
                // Insert at end
                std::string current = doc.toString();
                Atom new_atom = doc.localInsert(current.length(), c);
                
                auto bytes = BinaryPacker::pack(new_atom);
                sock.sendTo("127.0.0.1", peer_port, bytes);
            }
        }

        // --- 3. RENDER ---
        std::string current_text = doc.toString();
        if (current_text != last_render) {
            clearScreen();
            std::cout << "--- OmniSync Chat [Port " << my_port << "] ---\n";
            std::cout << current_text << "_"; // Cursor
            std::cout.flush();
            last_render = current_text;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Save CPU
    }

    return 0;
}
