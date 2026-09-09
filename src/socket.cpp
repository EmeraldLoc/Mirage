#include "socket.hpp"
#include <iostream>
#include <cstdlib>

bool sockaddrInEqual(const sockaddr_in &a, const sockaddr_in &b) {
    return a.sin_family == b.sin_family &&
           a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}

UDPSocket::UDPSocket(int port) {
#ifdef _WIN32
    WSADATA wsaData;
    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        std::cerr << "WSAStartup failed: " << res << std::endl;
        exit(1);
    }
#endif

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket." << std::endl;
        exit(1);
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#ifdef _WIN32
    DWORD timeout = 10;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // 10ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind to port " << port << std::endl;
        exit(1);
    }
}

UDPSocket::~UDPSocket() {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

int UDPSocket::getSock() const {
    return sock;
}

ssize_t UDPSocket::receive(uint8_t *buffer, size_t maxLen, sockaddr_in &clientAddr) {
    socklen_t clientLen = sizeof(clientAddr);
    return recvfrom(sock, reinterpret_cast<char*>(buffer), maxLen, 0, (struct sockaddr*)&clientAddr, &clientLen);
}