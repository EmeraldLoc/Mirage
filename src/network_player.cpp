#include "network_player.hpp"

std::array<NetworkPlayer, MAX_PLAYERS> gNetworkPlayers;
std::array<sockaddr_in, MAX_PLAYERS> gNetworkPlayerSockets;

NetworkPlayer *get_network_player_from_addr(const sockaddr_in &a) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (sockaddr_in_equal(a, gNetworkPlayerSockets[i])) {
            return &gNetworkPlayers[i];
        }
    }
    return nullptr;
}