#pragma once

#include <cstdint>
#include <string>
#include <array>
#include "socket.hpp"

constexpr int MAX_PLAYERS = 16;

enum NetworkSystemType {
    SYS_SOCKET = 0,
    SYS_COOPNET = 1
};

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

extern NetworkSystemType gNetworkSystemType;
extern std::array<NetworkPlayer, MAX_PLAYERS> gNetworkPlayers;
extern std::array<sockaddr_in, MAX_PLAYERS> gNetworkPlayerSockets;
extern std::array<uint64_t, MAX_PLAYERS> gNetworkPlayerPeerIds;

bool networkInit(NetworkSystemType type, int port);
void networkShutdown();
void updateNetwork();

NetworkPlayer* getNetworkPlayerFromAddr(const sockaddr_in &a);
NetworkPlayer* getNetworkPlayerFromPeerId(uint64_t peerId);
NetworkPlayer* getNetworkPlayerFromSender(const sockaddr_in &a, uint64_t peerId);

void networkSendTo(sockaddr_in addr, uint64_t peerId, const uint8_t* data, size_t len);
void networkSendToPlayer(int globalIndex, const uint8_t* data, size_t len);
void networkSendToAll(const uint8_t* data, size_t len, int ignoreIndex = -1);