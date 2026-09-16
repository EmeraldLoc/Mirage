#include "network.hpp"
#include "packet.hpp"
#include "log.hpp"
#include "config.hpp"
#include "libcoopnet.h"
#include <chrono>
#include <algorithm>
#include <cstring>

NetworkSystemType gNetworkSystemType = SYS_SOCKET;
std::array<NetworkPlayer, MAX_PLAYERS> gNetworkPlayers{};
std::array<sockaddr_in, MAX_PLAYERS> gNetworkPlayerSockets{};
std::array<uint64_t, MAX_PLAYERS> gNetworkPlayerPeerIds{};

static UDPSocket* sUdpSocket = nullptr;
static uint64_t sLocalUserId = 0;
static uint64_t sLocalLobbyId = 0;
static bool sNeedsLobbyCreate = false;

NetworkPlayer *getNetworkPlayerFromAddr(const sockaddr_in &a) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gNetworkPlayers[i].connected && sockaddrInEqual(a, gNetworkPlayerSockets[i])) {
            return &gNetworkPlayers[i];
        }
    }
    return nullptr;
}

NetworkPlayer *getNetworkPlayerFromPeerId(uint64_t peerId) {
    if (peerId == 0) return nullptr;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gNetworkPlayers[i].connected && gNetworkPlayerPeerIds[i] == peerId) {
            return &gNetworkPlayers[i];
        }
    }
    return nullptr;
}

NetworkPlayer *getNetworkPlayerFromSender(const sockaddr_in &a, uint64_t peerId) {
    if (gNetworkSystemType == SYS_COOPNET) {
        return getNetworkPlayerFromPeerId(peerId);
    } else {
        return getNetworkPlayerFromAddr(a);
    }
}

static void coopnetOnConnected(uint64_t userId) {
    Logging::log("NETWORK", "Connected to CoopNet! ID: {}", userId);
    sLocalUserId = userId;
}

static void coopnetOnDisconnected(bool intentional) {
    Logging::log("NETWORK", "Disconnected from CoopNet. Intentional: {}", intentional);
}

static void coopnetOnReceive(uint64_t userId, const uint8_t* data, uint64_t dataLength) {
    sockaddr_in dummyAddr{};
    std::memset(&dummyAddr, 0, sizeof(dummyAddr));
    CoopPacket pkt(0, dummyAddr, userId, data, dataLength);
    pkt.handle();
}

static void coopnetOnLobbyJoined(uint64_t lobbyId, uint64_t userId, uint64_t ownerId, uint64_t destId) {
    Logging::log("COOPNET", "Joined/Created lobby ID: {} (Owner: {})", lobbyId, ownerId);
    sLocalLobbyId = lobbyId;
}

static void coopnetOnLobbyLeft(uint64_t lobbyId, uint64_t userId) {
    Logging::log("COOPNET", "Left lobby ID: {}", lobbyId);
    if (lobbyId == sLocalLobbyId && userId == sLocalUserId) {
        sLocalLobbyId = 0;
    }
}

static void coopnetOnError(enum MPacketErrorNumber error, uint64_t tag) {
    Logging::log("COOPNET", "CoopNet Error: {} Tag: {}", static_cast<int>(error), tag);
}

static void coopnetOnPeerDisconnect(uint64_t peerId) {
    Logging::log("COOPNET", "Peer disconnected: {}", peerId);
    NetworkPlayer* np = getNetworkPlayerFromPeerId(peerId);
    if (np) {
        int idx = np->globalIndex;
        Logging::log("SERVER", "Player {} disconnected via CoopNet peer drop", np->name);
        np->connected = false;
        np->type = 0;
        np->globalIndex = 0;
        np->name = "";
        gNetworkPlayerPeerIds[idx] = 0;
        std::memset(&gNetworkPlayerSockets[idx], 0, sizeof(sockaddr_in));
    }
}

static void coopnetOnLoadBalance(const char* host, uint32_t port) {
    Logging::log("COOPNET", "Load balance directed to host: {} port: {}", host, port);
}

bool networkInit(NetworkSystemType type, int port) {
    gNetworkSystemType = type;

    if (gNetworkSystemType == SYS_SOCKET) {
        sUdpSocket = new UDPSocket(port);
        Logging::log("NETWORK", "Initialized Socket System on port {}", port);
        return true;
    } else if (gNetworkSystemType == SYS_COOPNET) {
        gCoopNetCallbacks.OnConnected = coopnetOnConnected;
        gCoopNetCallbacks.OnDisconnected = coopnetOnDisconnected;
        gCoopNetCallbacks.OnReceive = coopnetOnReceive;
        gCoopNetCallbacks.OnLobbyJoined = coopnetOnLobbyJoined;
        gCoopNetCallbacks.OnLobbyLeft = coopnetOnLobbyLeft;
        gCoopNetCallbacks.OnError = coopnetOnError;
        gCoopNetCallbacks.OnPeerDisconnected = coopnetOnPeerDisconnect;
        gCoopNetCallbacks.OnLoadBalance = coopnetOnLoadBalance;

        CoopNetRc rc = coopnet_begin("net.coop64.us", 34197, gServerConfig.name.c_str(), 0);
        if (rc == COOPNET_OK) {
            Logging::log("NETWORK", "Initialized CoopNet System");
            sNeedsLobbyCreate = true;
            return true;
        }
        Logging::log("NETWORK", "Failed to initialize CoopNet System");
        return false;
    }
    return false;
}

void networkShutdown() {
    if (sUdpSocket) {
        delete sUdpSocket;
        sUdpSocket = nullptr;
    }
    if (gNetworkSystemType == SYS_COOPNET) {
        coopnet_shutdown();
    }
}

void updateNetwork() {
    if (gNetworkSystemType == SYS_SOCKET) {
        if (sUdpSocket) {
            uint8_t buffer[PACKET_LENGTH];
            sockaddr_in clientAddr;
            std::memset(&clientAddr, 0, sizeof(clientAddr));
            ssize_t received = sUdpSocket->receive(buffer, sizeof(buffer), clientAddr);
            if (received > 0) {
                CoopPacket pkt(sUdpSocket->getSock(), clientAddr, 0, buffer, received);
                pkt.handle();
            }
        }
    } else if (gNetworkSystemType == SYS_COOPNET) {
        coopnet_update();
        if (sNeedsLobbyCreate && sLocalUserId != 0) {
            sNeedsLobbyCreate = false;
            CoopNetRc rc = coopnet_lobby_create("sm64coopdx", "v1.5.1", "horse", "horse", 16, "", "");
            if (rc == COOPNET_OK) {
                Logging::log("NETWORK", "Created CoopNet lobby successfully");
            }
        }
    }

    auto now = std::chrono::steady_clock::now();
    auto it = gReliablePackets.begin();

    while (it != gReliablePackets.end()) {
        std::chrono::duration<float> elapsed = now - it->lastSend;
        float maxElapsed = std::min(0.07f * (it->sendAttempts * it->sendAttempts), 4.0f);

        if (elapsed.count() > maxElapsed) {
            networkSendTo(it->addr, it->peerId, it->compressedData.data(), it->compressedData.size());

            it->lastSend = now;
            it->sendAttempts++;

            if (it->sendAttempts >= 15) {
                Logging::log("SERVER", "Giving up on reliable packet with seq {}", it->seqId);
                it = gReliablePackets.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void networkSendTo(sockaddr_in addr, uint64_t peerId, const uint8_t* data, size_t len) {
    if (gNetworkSystemType == SYS_SOCKET) {
        if (sUdpSocket) {
            sendto(sUdpSocket->getSock(), reinterpret_cast<const char*>(data), len, 0, (struct sockaddr*)&addr, sizeof(addr));
        }
    } else if (gNetworkSystemType == SYS_COOPNET) {
        if (peerId != 0) {
            coopnet_send_to(peerId, data, len);
        }
    }
}

void networkSendToPlayer(int globalIndex, const uint8_t* data, size_t len) {
    if (globalIndex >= 0 && globalIndex < MAX_PLAYERS) {
        networkSendTo(gNetworkPlayerSockets[globalIndex], gNetworkPlayerPeerIds[globalIndex], data, len);
    }
}

void networkSendToAll(const uint8_t* data, size_t len, int ignoreIndex) {
    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (!gNetworkPlayers[i].connected || i == ignoreIndex) continue;
        networkSendTo(gNetworkPlayerSockets[i], gNetworkPlayerPeerIds[i], data, len);
    }
}