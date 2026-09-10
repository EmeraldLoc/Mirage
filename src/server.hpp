#pragma once
#include "socket.hpp"

class CoopServer {
private:
    int port;
    UDPSocket udpSocket;
public:
    CoopServer(int p);
    void runLoop();
    int getPort() const { return port; }
};