#pragma once

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include <string>
#include <vector>
#include <iostream>

namespace omnisync {
namespace network {

/**
 * @brief Simple Cross-Platform UDP Socket Wrapper.
 * Hides the ugly C-style socket API behind a clean C++ class.
 */
class UdpSocket {
private:
    uint64_t sock;
    bool is_valid;
    struct sockaddr_in my_addr;

public:
    UdpSocket() : sock(0), is_valid(false) {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock != -1) is_valid = true;

        // Set Non-Blocking Mode
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    ~UdpSocket() {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
    }

    bool bind(int port) {
        if (!is_valid) return false;

        my_addr.sin_family = AF_INET;
        my_addr.sin_addr.s_addr = INADDR_ANY;
        my_addr.sin_port = htons(port);

        if (::bind(sock, (struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
            std::cerr << "Failed to bind to port " << port << std::endl;
            return false;
        }
        return true;
    }

    void sendTo(const std::string& ip, int port, const std::vector<uint8_t>& data) {
        struct sockaddr_in target;
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &target.sin_addr);

        sendto(sock, (const char*)data.data(), data.size(), 0, (struct sockaddr*)&target, sizeof(target));
    }

    bool receiveFrom(std::vector<uint8_t>& out_data, std::string& out_ip, int& out_port) {
        char buffer[4096];
        struct sockaddr_in sender;
        int sender_len = sizeof(sender);

        int len = recvfrom(sock, buffer, 4096, 0, (struct sockaddr*)&sender, &sender_len);
        
        if (len > 0) {
            out_data.assign(buffer, buffer + len);
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender.sin_addr, ip_str, INET_ADDRSTRLEN);
            out_ip = ip_str;
            out_port = ntohs(sender.sin_port);
            return true;
        }
        return false;
    }
};

} // namespace network
} // namespace omnisync
