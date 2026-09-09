#pragma once

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#undef WIN32_LEAN_AND_MEAN
typedef SOCKET socket_t;
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

class UDPSocket {
private:
    socket_t sock;
public:
    UDPSocket(int port);
    ~UDPSocket();
    
    int getSock() const;
    ssize_t receive(uint8_t *buffer, size_t maxLen, sockaddr_in &clientAddr);
};

extern bool sockaddrInEqual(const sockaddr_in &a, const sockaddr_in &b);
