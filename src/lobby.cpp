#include "lobby.hpp"
#include <iostream>

CoopLobby::CoopLobby(int p) : server(p) {}

void CoopLobby::start() {
    server.runLoop();
}