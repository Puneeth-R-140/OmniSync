#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <cerrno>
#endif

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace omnisync {
namespace network {

#ifdef _WIN32
using SocketHandle = SOCKET;
#else
using SocketHandle = int;
#endif

/**
 * @brief Simple cross-platform UDP socket wrapper.
 *
 * The socket is non-blocking by default so receiveFrom() can be polled without
 * stalling the CRDT/networking loop.
 *
 * This class owns its socket and is intentionally non-copyable. A UDP send is
 * considered successful only when the operating system accepts the complete
 * datagram. The wrapper does not provide delivery guarantees; callers that
 * need reliability must implement acknowledgements/retransmission above UDP.
 */
class UdpSocket {
private:
#ifdef _WIN32
    static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
    static constexpr SocketHandle kInvalidSocket = -1;
#endif

    SocketHandle sock_ = kInvalidSocket;
    bool is_valid_ = false;

#ifdef _WIN32
    bool winsock_started_ = false;
#endif

    struct sockaddr_in my_addr_{};

    static bool validPort(int port) noexcept {
        return port >= 0 && port <= 65535;
    }

    static bool validDatagramSize(std::size_t size) noexcept {
        // IPv4 UDP payload maximum is 65,507 bytes.
        return size <= 65507u;
    }

    void closeSocket() noexcept {
        if (!is_valid_) return;

#ifdef _WIN32
        closesocket(sock_);
#else
        ::close(sock_);
#endif
        sock_ = kInvalidSocket;
        is_valid_ = false;
    }

public:
    UdpSocket() {
#ifdef _WIN32
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return;
        }
        winsock_started_ = true;
#endif

        sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

#ifdef _WIN32
        if (sock_ == INVALID_SOCKET) {
            WSACleanup();
            winsock_started_ = false;
            return;
        }
#else
        if (sock_ < 0) {
            return;
        }
#endif

        is_valid_ = true;

        // Set non-blocking mode.
#ifdef _WIN32
        u_long mode = 1;
        if (ioctlsocket(sock_, FIONBIO, &mode) != 0) {
            closeSocket();
            WSACleanup();
            winsock_started_ = false;
        }
#else
        const int flags = fcntl(sock_, F_GETFL, 0);
        if (flags < 0 || fcntl(sock_, F_SETFL, flags | O_NONBLOCK) != 0) {
            closeSocket();
        }
#endif
    }

    ~UdpSocket() {
        closeSocket();
#ifdef _WIN32
        if (winsock_started_) {
            WSACleanup();
            winsock_started_ = false;
        }
#endif
    }

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&& other) noexcept
        : sock_(other.sock_),
          is_valid_(other.is_valid_),
          my_addr_(other.my_addr_)
#ifdef _WIN32
          , winsock_started_(other.winsock_started_)
#endif
    {
        other.sock_ = kInvalidSocket;
        other.is_valid_ = false;
#ifdef _WIN32
        other.winsock_started_ = false;
#endif
    }

    UdpSocket& operator=(UdpSocket&& other) noexcept {
        if (this == &other) return *this;

        closeSocket();
#ifdef _WIN32
        if (winsock_started_) {
            WSACleanup();
            winsock_started_ = false;
        }
#endif

        sock_ = other.sock_;
        is_valid_ = other.is_valid_;
        my_addr_ = other.my_addr_;
#ifdef _WIN32
        winsock_started_ = other.winsock_started_;
#endif

        other.sock_ = kInvalidSocket;
        other.is_valid_ = false;
#ifdef _WIN32
        other.winsock_started_ = false;
#endif
        return *this;
    }

    bool isValid() const noexcept { return is_valid_; }

    bool bind(int port) {
        if (!is_valid_ || !validPort(port)) return false;

        my_addr_ = {};
        my_addr_.sin_family = AF_INET;
        my_addr_.sin_addr.s_addr = htonl(INADDR_ANY);
        my_addr_.sin_port = htons(static_cast<uint16_t>(port));

#ifdef _WIN32
        const int result = ::bind(
            sock_,
            reinterpret_cast<const sockaddr*>(&my_addr_),
            static_cast<int>(sizeof(my_addr_)));
#else
        const int result = ::bind(
            sock_,
            reinterpret_cast<const sockaddr*>(&my_addr_),
            static_cast<socklen_t>(sizeof(my_addr_)));
#endif

        return result == 0;
    }

    /**
     * @brief Send one UDP datagram.
     * @return true when the complete datagram was accepted by the OS.
     */
    bool sendTo(
        const std::string& ip,
        int port,
        const std::vector<uint8_t>& data) {
        if (!is_valid_ || !validPort(port) || !validDatagramSize(data.size())) {
            return false;
        }

        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(static_cast<uint16_t>(port));

        if (inet_pton(AF_INET, ip.c_str(), &target.sin_addr) != 1) {
            return false;
        }

#ifdef _WIN32
        const int length = static_cast<int>(data.size());
        const int sent = ::sendto(
            sock_,
            reinterpret_cast<const char*>(data.data()),
            length,
            0,
            reinterpret_cast<const sockaddr*>(&target),
            static_cast<int>(sizeof(target)));
#else
        const ssize_t sent = ::sendto(
            sock_,
            data.empty() ? nullptr :
                reinterpret_cast<const void*>(data.data()),
            data.size(),
            0,
            reinterpret_cast<const sockaddr*>(&target),
            static_cast<socklen_t>(sizeof(target)));
#endif

        return sent == static_cast<decltype(sent)>(data.size());
    }

    /**
     * @brief Receive one UDP datagram without blocking.
     *
     * @return true when a datagram was received. False means either that no
     * datagram is currently available or that the receive failed.
     */
    bool receiveFrom(
        std::vector<uint8_t>& out_data,
        std::string& out_ip,
        int& out_port) {
        if (!is_valid_) return false;

        // Maximum IPv4 UDP payload. Keeping this exact avoids silently
        // truncating a valid datagram.
        std::vector<uint8_t> buffer(65507u);

        sockaddr_in sender{};
#ifdef _WIN32
        int sender_len = static_cast<int>(sizeof(sender));
        const int len = ::recvfrom(
            sock_,
            reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()),
            0,
            reinterpret_cast<sockaddr*>(&sender),
            &sender_len);
#else
        socklen_t sender_len = static_cast<socklen_t>(sizeof(sender));
        const ssize_t len = ::recvfrom(
            sock_,
            reinterpret_cast<char*>(buffer.data()),
            buffer.size(),
            0,
            reinterpret_cast<sockaddr*>(&sender),
            &sender_len);
#endif

        if (len < 0) {
            out_data.clear();
            return false;
        }

        if (inet_ntop(
                AF_INET,
                &sender.sin_addr,
                ipBuffer_,
                static_cast<socklen_t>(sizeof(ipBuffer_))) == nullptr) {
            out_data.clear();
            return false;
        }

        out_data.assign(
            buffer.begin(),
            buffer.begin() + static_cast<std::size_t>(len));
        out_ip = ipBuffer_;
        out_port = static_cast<int>(ntohs(sender.sin_port));
        return true;
    }

private:
    char ipBuffer_[INET_ADDRSTRLEN]{};
};

} // namespace network
} // namespace omnisync
