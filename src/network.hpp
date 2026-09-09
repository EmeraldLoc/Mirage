#pragma once

#include <cstdint>
#include <string>
#include <array>

#include "socket.hpp"

#define MAX_PLAYERS 16

struct PlayerPalette {
    uint8_t colors[24];
};

struct NetworkPlayer {
    bool connected = false;
    uint8_t type = 0;
    uint8_t globalIndex = 0;
    uint16_t currLevelAreaSeqId = 0;
    int16_t currCourseNum = 0;
    int16_t currActNum = 0;
    int16_t currLevelNum = 0;
    int16_t currAreaIndex = 0;
    uint8_t currLevelSyncValid = 0;
    uint8_t currAreaSyncValid = 0;
    int64_t networkId = 0;
    uint8_t modelIndex = 0;
    PlayerPalette palette{};
    std::string name;
    std::string discordId;
};

extern std::array<NetworkPlayer, MAX_PLAYERS> gNetworkPlayers;
extern std::array<sockaddr_in, MAX_PLAYERS> gNetworkPlayerSockets;

extern NetworkPlayer *getNetworkPlayerFromAddr(const sockaddr_in &a);
extern void updateNetwork();