#include "server.hpp"
#include "packet.hpp"
#include "network.hpp"
#include "log.hpp"

CoopServer::CoopServer(int p) : port(p), udpSocket(p) {}

void CoopServer::runLoop() {
    uint8_t buffer[PACKET_LENGTH];
    sockaddr_in clientAddr;

    Logging::log("SERVER", "Starting server on port {}", port);

    while (true) {
        ssize_t received = udpSocket.receive(buffer, sizeof(buffer), clientAddr);
        
        if (received > 0) {
            CoopPacket pkt(udpSocket.getSock(), clientAddr, buffer, received);
            pkt.handle();
        }

        updateNetwork();
    }
}