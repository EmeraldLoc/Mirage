#include "server.hpp"
#include "packet.hpp"
#include "network.hpp"
#include <iostream>

CoopServer::CoopServer(int p) : port(p), udpSocket(p) {}

void CoopServer::runLoop() {
    uint8_t buffer[PACKET_LENGTH];
    sockaddr_in clientAddr;

    std::cout << "Server listening on port " << port << std::endl;

    while (true) {
        ssize_t received = udpSocket.receive(buffer, sizeof(buffer), clientAddr);
        
        if (received > 0) {
            CoopPacket pkt(udpSocket.getSock(), clientAddr, buffer, received);
            pkt.handle();
        }

        updateNetwork();
    }
}