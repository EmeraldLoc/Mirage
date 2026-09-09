#pragma once

#include <vector>
#include <cstdint>
#include "socket.hpp"

class CoopLobby {
private:
    int port;
    UDPSocket udpSocket;

    std::vector<uint8_t> decompressData(const uint8_t *data, size_t len);
public:
    CoopLobby(int p);

    void start();
    void runLoop();
};