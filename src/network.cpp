#include "network.hpp"
#include "packet.hpp"

std::array<NetworkPlayer, MAX_PLAYERS> gNetworkPlayers;
std::array<sockaddr_in, MAX_PLAYERS> gNetworkPlayerSockets;

NetworkPlayer *getNetworkPlayerFromAddr(const sockaddr_in &a) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (sockaddrInEqual(a, gNetworkPlayerSockets[i])) {
            return &gNetworkPlayers[i];
        }
    }
    return nullptr;
}

void updateNetwork() {
    auto now = std::chrono::steady_clock::now();
    auto it = gReliablePackets.begin();

    while (it != gReliablePackets.end()) {
        std::chrono::duration<float> elapsed = now - it->lastSend;
        float maxElapsed = std::min(0.07f * (it->sendAttempts * it->sendAttempts), 4.0f);

        if (elapsed.count() > maxElapsed) {
            sendto(it->sock, reinterpret_cast<const char*>(it->compressedData.data()), it->compressedData.size(), 0, (struct sockaddr*)&it->addr, sizeof(it->addr));

            it->lastSend = now;
            it->sendAttempts++;

            if (it->sendAttempts >= 15) {
                std::cout << "Dropping reliable packet seq " << it->seqId << ", max attempts reached\n";
                it = gReliablePackets.erase(it);
                continue;
            }
        }
        ++it;
    }
}