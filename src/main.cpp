#include <cstring>
#include "config.hpp"
#include "network.hpp"
#include "lobby.hpp"
#include "savefile.hpp"

int main() {
    gServerConfig.read(SERVER_CONFIGFILE);

    gNetworkPlayers[0].type = NPT_LOCAL;
    gNetworkPlayers[0].globalIndex = 0;
    gNetworkPlayers[0].connected = true;
    gNetworkPlayers[0].name = gServerConfig.name;
    gNetworkPlayers[0].currLevelNum = 16;
    gNetworkPlayers[0].currAreaIndex = 1;

    gSaveFile.setIndex(gServerConfig.savefileIndex);
    gSaveFile.load();

    CoopLobby lobby(gServerConfig.port);
    lobby.start();

    return 0;
}