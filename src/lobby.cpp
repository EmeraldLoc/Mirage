#include "lobby.hpp"
#include "network.hpp"
#include "packet.hpp"
#include <iostream>
#include <zlib.h>

CoopLobby::CoopLobby(int p) : port(p), udpSocket(p) {}

std::vector<uint8_t> CoopLobby::decompressData(const uint8_t *data, size_t len) {
    std::vector<uint8_t> dest(65536);
    uLongf destLen = dest.size();
    if (uncompress(dest.data(), &destLen, data, len) == Z_OK) {
        dest.resize(destLen);
        return dest;
    }
    return {};
}

void CoopLobby::start() {
    std::cout << "Headless lobby listening on UDP port " << port << std::endl;
    runLoop();
}

void CoopLobby::runLoop() {
    uint8_t buffer[PACKET_LENGTH];
    sockaddr_in clientAddr;

    while (true) {
        ssize_t received = udpSocket.receive(buffer, sizeof(buffer), clientAddr);
        
        if (received > 0) {
            std::vector<uint8_t> decompressed = decompressData(buffer, received);
            if (!decompressed.empty()) {
                CoopPacket pkt(udpSocket.getSock(), clientAddr, decompressed);
                pkt.handle();
            }
        }

        updateNetwork();
    }
}