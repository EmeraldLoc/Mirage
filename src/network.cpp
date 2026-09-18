#include "network.hpp"
#include "packet.hpp"
#include "log.hpp"
#include "config.hpp"
#include <chrono>
#include <algorithm>
#include <cstring>
#include <thread>

std::array<NetworkPlayer, MAX_PLAYERS> gNetworkPlayers{};
std::array<sockaddr_in, MAX_PLAYERS> gNetworkPlayerSockets{};
std::array<uint64_t, MAX_PLAYERS> gNetworkPlayerPeerIds{};
std::unique_ptr<NetworkSystem> gNetworkSystem = nullptr;

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

NetworkPlayer *getNetworkPlayerFromLevel(int16_t courseNum, int16_t actNum, int16_t levelNum) {
    for (auto &np : gNetworkPlayers) {
        if (!np.connected)                 { continue; }
        if (!np.currLevelSyncValid)        { continue; }
        if (np.currCourseNum != courseNum) { continue; }
        if (np.currActNum    != actNum)    { continue; }
        if (np.currLevelNum  != levelNum)  { continue; }
        return &np;
    }
    return nullptr;
}

NetworkPlayer *getNetworkPlayerFromArea(int16_t courseNum, int16_t actNum, int16_t levelNum, int16_t areaIndex) {
    for (auto &np : gNetworkPlayers) {
        if (!np.connected)                 { continue; }
        if (!np.currLevelSyncValid)        { continue; }
        if (!np.currAreaSyncValid)         { continue; }
        if (np.currCourseNum != courseNum) { continue; }
        if (np.currActNum    != actNum)    { continue; }
        if (np.currLevelNum  != levelNum)  { continue; }
        if (np.currAreaIndex != areaIndex) { continue; }
        return &np;
    }
    return nullptr;
}

void NetworkSystem::sendToPlayer(int globalIndex, const uint8_t *data, size_t len) {
    if (globalIndex >= 0 && globalIndex < MAX_PLAYERS) {
        sendTo(gNetworkPlayerSockets[globalIndex], gNetworkPlayerPeerIds[globalIndex], data, len);
    }
}

void NetworkSystem::sendToAll(const uint8_t *data, size_t len, int ignoreIndex) {
    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (!gNetworkPlayers[i].connected || i == ignoreIndex) continue;
        sendTo(gNetworkPlayerSockets[i], gNetworkPlayerPeerIds[i], data, len);
    }
}

void NetworkSystem::updateReliablePackets() {
    auto now = std::chrono::steady_clock::now();
    auto it = gReliablePackets.begin();

    while (it != gReliablePackets.end()) {
        std::chrono::duration<float> elapsed = now - it->lastSend;
        float maxElapsed = std::min(0.07f * (it->sendAttempts * it->sendAttempts), 4.0f);

        if (elapsed.count() > maxElapsed) {
            sendTo(it->addr, it->peerId, it->compressedData.data(), it->compressedData.size());

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

bool SocketNetworkSystem::init(int port) {
    udpSock = new UDPSocket(port);
    Logging::log("NETWORK", "Initialized Socket System on port {}", port);
    return true;
}

void SocketNetworkSystem::shutdown() {
    if (udpSock) {
        delete udpSock;
        udpSock = nullptr;
    }
}

void SocketNetworkSystem::update() {
    if (udpSock) {
        uint8_t buffer[PACKET_LENGTH];
        sockaddr_in clientAddr;
        std::memset(&clientAddr, 0, sizeof(clientAddr));
        ssize_t received = udpSock->receive(buffer, sizeof(buffer), clientAddr);
        if (received > 0) {
            CoopPacket pkt(udpSock->getSock(), clientAddr, 0, buffer, received);
            pkt.handle();
        }
    }
    updateReliablePackets();
}

void SocketNetworkSystem::sendTo(const sockaddr_in &addr, uint64_t peerId, const uint8_t *data, size_t len) {
    (void)peerId;
    if (udpSock) {
        sendto(udpSock->getSock(), reinterpret_cast<const char*>(data), len, 0, (struct sockaddr*)&addr, sizeof(addr));
    }
}

bool SocketNetworkSystem::isSameEndpoint(const sockaddr_in &a1, uint64_t p1, const sockaddr_in &a2, uint64_t p2) {
    (void)p1; (void)p2;
    return sockaddrInEqual(a1, a2);
}

NetworkPlayer *SocketNetworkSystem::getPlayerFromSender(const sockaddr_in &a, uint64_t peerId) {
    (void)peerId;
    return getNetworkPlayerFromAddr(a);
}

void CoopNetNetworkSystem::onConnected(uint64_t userId) {
    Logging::log("NETWORK", "Connected to CoopNet! ID: {}", userId);
    auto *coopnet = static_cast<CoopNetNetworkSystem*>(gNetworkSystem.get());
    if (coopnet) {
        coopnet->setLocalUserId(userId);
    }
}

void CoopNetNetworkSystem::onDisconnected(bool intentional) {
    Logging::log("NETWORK", "Disconnected from CoopNet. Intentional: {}", intentional);
}

void CoopNetNetworkSystem::onReceive(uint64_t userId, const uint8_t *data, uint64_t dataLength) {
    sockaddr_in dummyAddr{};
    std::memset(&dummyAddr, 0, sizeof(dummyAddr));
    CoopPacket pkt(0, dummyAddr, userId, data, dataLength);
    pkt.handle();
}

void CoopNetNetworkSystem::onLobbyJoined(uint64_t lobbyId, uint64_t userId, uint64_t ownerId, uint64_t destId) {
    Logging::log("NETWORK", "Joined/Created lobby ID: {} (Owner: {})", lobbyId, ownerId);
    auto *coopnet = static_cast<CoopNetNetworkSystem*>(gNetworkSystem.get());
    if (coopnet) {
        coopnet->setLocalLobbyId(lobbyId);
    }
}

void CoopNetNetworkSystem::onLobbyLeft(uint64_t lobbyId, uint64_t userId) {
    Logging::log("NETWORK", "Left lobby ID: {}", lobbyId);
    auto *coopnet = static_cast<CoopNetNetworkSystem*>(gNetworkSystem.get());
    if (coopnet) {
        if (lobbyId == coopnet->getLocalLobbyId() && userId == coopnet->getLocalUserId()) {
            coopnet->setLocalLobbyId(0);
        }
    }
}

void CoopNetNetworkSystem::onError(enum MPacketErrorNumber error, uint64_t tag) {
    Logging::log("NETWORK", "CoopNet Error: {} Tag: {}", (int)error, tag);
}

void CoopNetNetworkSystem::onPeerConnect(uint64_t peerId) {
    Logging::log("NETWORK", "Peer connected: {}", peerId);
}

void CoopNetNetworkSystem::onPeerDisconnect(uint64_t peerId) {
    Logging::log("NETWORK", "Peer disconnected: {}", peerId);
    NetworkPlayer *np = getNetworkPlayerFromPeerId(peerId);
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

void CoopNetNetworkSystem::onLoadBalance(const char* host, uint32_t port) {
    Logging::log("NETWORK", "Load balance directed to host: {} port: {}", host, port);
}

bool CoopNetNetworkSystem::init(int port) {
    (void)port;
    gCoopNetCallbacks.OnConnected = CoopNetNetworkSystem::onConnected;
    gCoopNetCallbacks.OnDisconnected = CoopNetNetworkSystem::onDisconnected;
    gCoopNetCallbacks.OnReceive = CoopNetNetworkSystem::onReceive;
    gCoopNetCallbacks.OnLobbyJoined = CoopNetNetworkSystem::onLobbyJoined;
    gCoopNetCallbacks.OnLobbyLeft = CoopNetNetworkSystem::onLobbyLeft;
    gCoopNetCallbacks.OnError = CoopNetNetworkSystem::onError;
    gCoopNetCallbacks.OnPeerConnected = CoopNetNetworkSystem::onPeerConnect;
    gCoopNetCallbacks.OnPeerDisconnected = CoopNetNetworkSystem::onPeerDisconnect;
    gCoopNetCallbacks.OnLoadBalance = CoopNetNetworkSystem::onLoadBalance;

    CoopNetRc rc = coopnet_begin("net.coop64.us", 34197, gServerConfig.name.c_str(), 0);
    if (rc == COOPNET_OK) {
        Logging::log("NETWORK", "Initialized CoopNet System");
        needsLobbyCreate = true;
        return true;
    }
    Logging::log("NETWORK", "Failed to initialize CoopNet System");
    return false;
}

void CoopNetNetworkSystem::shutdown() {
    coopnet_shutdown();
}

void CoopNetNetworkSystem::update() {
    coopnet_update();
    if (needsLobbyCreate && localUserId != 0) {
        needsLobbyCreate = false;
        CoopNetRc rc = coopnet_lobby_create(gServerConfig.game.c_str(), gServerConfig.version.c_str(), gServerConfig.name.c_str(), gServerConfig.mode.c_str(), 16, gServerConfig.password.c_str(), gServerConfig.description.c_str());
        if (rc == COOPNET_OK) {
            Logging::log("NETWORK", "Created CoopNet lobby successfully");
        }
    }
    updateReliablePackets();
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
}

void CoopNetNetworkSystem::sendTo(const sockaddr_in &addr, uint64_t peerId, const uint8_t *data, size_t len) {
    (void)addr;
    if (peerId != 0) {
        coopnet_send_to(peerId, data, len);
    }
}

bool CoopNetNetworkSystem::isSameEndpoint(const sockaddr_in &a1, uint64_t p1, const sockaddr_in &a2, uint64_t p2) {
    (void)a1; (void)a2;
    return (p1 != 0 && p1 == p2);
}

NetworkPlayer *CoopNetNetworkSystem::getPlayerFromSender(const sockaddr_in &a, uint64_t peerId) {
    (void)a;
    return getNetworkPlayerFromPeerId(peerId);
}

bool networkInit(NetworkSystemType type, int port) {
    if (type == SYS_SOCKET) {
        gNetworkSystem = std::make_unique<SocketNetworkSystem>();
    } else if (type == SYS_COOPNET) {
        gNetworkSystem = std::make_unique<CoopNetNetworkSystem>();
    } else {
        return false;
    }

    return gNetworkSystem->init(port);
}

void networkShutdown() {
    if (gNetworkSystem) {
        Logging::log("SERVER", "Shutting down");
        gNetworkSystem->shutdown();
        gNetworkSystem.reset();
    }
}
