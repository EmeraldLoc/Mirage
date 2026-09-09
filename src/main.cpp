#include <cstring>
#include "network.hpp"
#include "lobby.hpp"

int main() {
    gNetworkPlayers[0].type = 2;
    gNetworkPlayers[0].globalIndex = 0;
    gNetworkPlayers[0].connected = true;
    gNetworkPlayers[0].name = "PeakServer";
    gNetworkPlayers[0].currLevelNum = 16;
    gNetworkPlayers[0].currAreaIndex = 1;
    memset(gNetworkPlayers[0].palette.colors, 0xff, 24);
    
    CoopLobby lobby(46922);
    lobby.start();
    
    return 0;
}