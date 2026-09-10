#pragma once
#include "server.hpp"

class CoopLobby {
private:
    CoopServer server;
public:
    CoopLobby(int p);
    void start();
};