#include "packet.hpp"
#include "network.hpp"
#include "config.hpp"
#include <algorithm>
#include <ostream>
#include <zlib.h>
#include <cstdint>

std::list<ReliablePacket> gReliablePackets;
std::map<std::pair<uint8_t, uint16_t>, OrderedState> gOrderedStates;

static bool sOrderedPackets = false;
static uint16_t sCurrentOrderedGroupId = 0;
static uint16_t sCurrentOrderedSeqId = 0;
static uint16_t sNextSeqNum = 1;

inline bool getBit(uint8_t val, uint8_t num) {
    return (val >> num) & 1;
}

void packetOrderedBegin() {
    if (sOrderedPackets) return;
    sOrderedPackets = true;

    sCurrentOrderedGroupId++;
    if (sCurrentOrderedGroupId == 0) sCurrentOrderedGroupId++;
    sCurrentOrderedSeqId = 1;
}

void packetOrderedEnd() {
    sOrderedPackets = false;
    sCurrentOrderedSeqId = 0;
}

CoopPacket::CoopPacket(socket_t s, sockaddr_in a, const std::vector<uint8_t> &data) : sock(s), addr(a), rawData(data) {
    if (rawData.size() < 3) return;

    pktType = rawData[0];
    seqId = rawData[1] | (rawData[2] << 8);
    offset = 3;
    flags = read<uint8_t>();
    
    levelAreaMustMatch = getBit(flags, 0);
    requestBroadcast = getBit(flags, 1);
    isOrdered = getBit(flags, 2);
    levelMustMatch = getBit(flags, 3);

    destGlobalId = read<uint8_t>();

    if (isOrdered) {
        orderedFromGlobalId = read<uint8_t>();
        orderedGroupId = read<uint16_t>();
        orderedSeqId = read<uint16_t>();
    }

    if (levelAreaMustMatch) {
        courseNum = read<uint8_t>();
        actNum = read<uint8_t>();
        levelNum = read<int16_t>();
        areaIndex = read<uint8_t>();
    } else if (levelMustMatch) {
        courseNum = read<uint8_t>();
        actNum = read<uint8_t>();
        levelNum = read<int16_t>();
    }
}

void CoopPacket::setOrderedData() {
    if (this->orderedGroupId == 0) return;
    if (this->orderedSeqId != 0) return;
    
    this->orderedSeqId = sCurrentOrderedSeqId++;
    
    if (outBuffer.size() >= 10) {
        outBuffer[8] = this->orderedSeqId & 0xFF;
        outBuffer[9] = (this->orderedSeqId >> 8) & 0xFF;
    }
}

void CoopPacket::sendBuffer() {
    if (outBuffer.empty()) return;

    if (this->isOrdered) {
        setOrderedData();
    }

    uint32_t hashVal = 0;
    int bytePos = 0;
    for (uint8_t b : outBuffer) {
        hashVal ^= ((uint32_t)(b) << (8 * bytePos));
        bytePos = (bytePos + 1) % 4;
    }
    write<uint32_t>(hashVal);

    std::vector<uint8_t> compressed(compressBound(outBuffer.size()));
    uLongf destLen = compressed.size();
    if (compress2(compressed.data(), &destLen, outBuffer.data(), outBuffer.size(), Z_BEST_COMPRESSION) == Z_OK) {
        compressed.resize(destLen);
        sendto(sock, reinterpret_cast<const char*>(compressed.data()), compressed.size(), 0, (struct sockaddr*)&addr, sizeof(addr));

        if (this->isReliable && this->seqId != 0) {
            gReliablePackets.push_back({
                this->seqId,
                this->sock,
                this->addr,
                compressed,
                std::chrono::steady_clock::now(),
                1
            });
        }
    }
    outBuffer.clear();
}

void CoopPacket::sendBufferToAll() {
    if (outBuffer.empty()) return;

    if (this->isOrdered) {
        setOrderedData();
    }

    uint32_t hashVal = 0;
    int bytePos = 0;

    for (uint8_t b : outBuffer) {
        hashVal ^= ((uint32_t)b << (8 * bytePos));
        bytePos = (bytePos + 1) % 4;
    }

    write<uint32_t>(hashVal);

    std::vector<uint8_t> compressed(compressBound(outBuffer.size()));
    uLongf destLen = compressed.size();
    if (compress(compressed.data(), &destLen, outBuffer.data(), outBuffer.size()) == Z_OK) {
        compressed.resize(destLen);

        for (int i = 1; i < MAX_PLAYERS; i++) {
            if (!gNetworkPlayers[i].connected || sockaddrInEqual(addr, gNetworkPlayerSockets[i])) continue;

            sendto(sock, reinterpret_cast<const char*>(compressed.data()), compressed.size(), 0, (struct sockaddr*)&gNetworkPlayerSockets[i], sizeof(gNetworkPlayerSockets[i]));

            if (this->isReliable && this->seqId != 0) {
                gReliablePackets.push_back({
                    this->seqId,
                    this->sock,
                    gNetworkPlayerSockets[i],
                    compressed,
                    std::chrono::steady_clock::now(),
                    1
                });
            }
        }
    }

    outBuffer.clear();
}

void CoopPacket::packetInit(uint8_t pType, bool reliable, uint8_t levelMatchType) {
    outBuffer.clear();
    uint8_t initFlags = 0;
    
    if (levelMatchType == PLMT_AREA) initFlags |= (1 << 0);
    if (levelMatchType == PLMT_LEVEL) initFlags |= (1 << 3);
    if (sOrderedPackets) initFlags |= (1 << 2);

    uint16_t currentSeq = 0;
    if (reliable) {
        currentSeq = sNextSeqNum++;
        if (sNextSeqNum == 0) sNextSeqNum = 1;
    }

    write<uint8_t>(pType);
    write<uint16_t>(currentSeq);
    write<uint8_t>(initFlags);
    write<uint8_t>(PACKET_DESTINATION_BROADCAST);

    NetworkPlayer *localNp = &gNetworkPlayers[0];
    if (sOrderedPackets) {
        uint8_t localGlobalIndex = localNp->globalIndex; 
        write<uint8_t>(sOrderedPackets ? localGlobalIndex : 0); 
        write<uint16_t>(sOrderedPackets ? sCurrentOrderedGroupId : 0); 
        write<uint16_t>(0);
    }
    
    if (levelMatchType == PLMT_AREA) {
        write<uint8_t>(localNp->currCourseNum);
        write<uint8_t>(localNp->currActNum);
        write<int16_t>(localNp->currLevelNum);
        write<uint8_t>(localNp->currAreaIndex);
    } else if (levelMatchType == PLMT_LEVEL) {
        write<uint8_t>(localNp->currCourseNum);
        write<uint8_t>(localNp->currActNum);
        write<int16_t>(localNp->currLevelNum);
    }

    this->isReliable = reliable;
    this->seqId = currentSeq;
    this->isOrdered = (sOrderedPackets);
    this->orderedGroupId = sOrderedPackets ? sCurrentOrderedGroupId : 0;
    this->orderedSeqId = 0;
}

void CoopPacket::handle() {
    if (seqId != 0 && pktType != PACKET_ACK) {
        CoopPacket ackPkt(sock, addr, {});
        ackPkt.packetInit(PACKET_ACK, false, PLMT_NONE);
        ackPkt.write<uint16_t>(seqId);
        ackPkt.sendBuffer();
    }

    if (isOrdered) {
        processOrderedAndHandle();
    } else {
        handleInternal();
    }
}

void CoopPacket::processOrderedAndHandle() {
    std::pair<uint8_t, uint16_t> key = {orderedFromGlobalId, orderedGroupId};
    auto &state = gOrderedStates[key];

    if (orderedSeqId < state.processSeqId) {
        return;
    }

    state.queuedPackets.emplace(orderedSeqId, *this);

    auto it = state.queuedPackets.begin();
    while (it != state.queuedPackets.end() && it->first == state.processSeqId) {
        it->second.handleInternal();
        it = state.queuedPackets.erase(it);
        state.processSeqId++;
    }
}

void CoopPacket::forwardAndIgnore(uint8_t pType, bool reliable, uint8_t levelMatchType) {
    int32_t size = rawData.size();
    std::vector<uint8_t> data(size);
    for (int i = 0; i < size; i++) {
        data[i] = read<uint8_t>();
    }

    packetInit(pType, reliable, levelMatchType);
    for (int i = 0; i < size; i++) {
        write<uint8_t>(data[i]);
    }
    sendBufferToAll();
}

void CoopPacket::handleInternal() {
    switch (pktType) {
        case PACKET_ACK: {
            uint16_t ackedSeq = read<uint16_t>();
            gReliablePackets.remove_if([this, ackedSeq](const ReliablePacket &p) {
                return p.seqId == ackedSeq && sockaddrInEqual(p.addr, this->addr);
            });
            break;
        }
        case PACKET_PLAYER:
            // todo: should be plmt area
            forwardAndIgnore(PACKET_PLAYER, true, PLMT_NONE);
            break;
        case PACKET_OBJECT:
            // todo: should be plmt area
            forwardAndIgnore(PACKET_OBJECT, true, PLMT_NONE);
            break;
        case PACKET_COLLECT_COIN:
            // todo: should be plmt level
            forwardAndIgnore(PACKET_COLLECT_COIN, true, PLMT_NONE);
            break;
        case PACKET_LEVEL_RESPAWN_INFO:
            forwardAndIgnore(PACKET_LEVEL_RESPAWN_INFO, true, PLMT_NONE);
            break;
        case PACKET_MOD_LIST_REQUEST: {
            std::string version = read<std::string>(128);
            std::cout << "Received mod list request:\n  Version: " << version.c_str() << std::endl;

            packetOrderedBegin();

            packetInit(PACKET_MOD_LIST, true, PLMT_NONE);
            write<std::string>(gServerConfig.version, 128);
            write<uint16_t>(0);
            sendBuffer();

            packetInit(PACKET_MOD_LIST_DONE, true, PLMT_NONE);

            sendBuffer();

            packetOrderedEnd();
            break;
        }
        case PACKET_JOIN_REQUEST: {
            bool exists = std::find_if(gNetworkPlayerSockets.begin(), gNetworkPlayerSockets.end(),
                [&](const sockaddr_in &address) {
                    return sockaddrInEqual(address, addr);
                }) != gNetworkPlayerSockets.end();
            if (exists) {
                std::cout << "Received join request from already joined socket, ignoring" << std::endl;
                break;
            }
            std::string version = read<std::string>(128);
            uint8_t model = read<uint8_t>();
            PlayerPalette palette;
            for (int i = 0; i < 24; i++) {
                palette.colors[i] = read<uint8_t>();
            }
            std::string name = read<std::string>(64);

            std::cout << "Received join request:\n  Version: " << version.c_str() << "\n  Name: " << name.c_str() << std::endl;

            uint8_t globalIndex = 0;
            uint8_t connectedCount = 0;
            for (uint8_t i = 1; i < MAX_PLAYERS; i++) {
                if (!gNetworkPlayers[i].connected) {
                    globalIndex = i;
                    break;
                } else {
                    connectedCount++;
                }
            }

            std::cout << "Connections: " << (int)connectedCount << std::endl;
            if (!globalIndex) {
                std::cout << "No available global indices, server full, dropping request from " << name << std::endl;
                break;
            }

            NetworkPlayer *np = &gNetworkPlayers[globalIndex];
            gNetworkPlayerSockets[globalIndex] = addr;

            packetInit(PACKET_JOIN, true, PLMT_NONE);

            write<std::string>(gServerConfig.version, 128);
            write<uint8_t>(globalIndex);

            write<int16_t>(gServerConfig.savefileIndex);
            write<uint8_t>(gServerConfig.playerInteractions);
            write<uint8_t>(gServerConfig.bouncyBounds);
            write<uint8_t>(gServerConfig.knockStrength);
            write<uint8_t>(gServerConfig.starStaying);
            write<uint8_t>(gServerConfig.skipIntro);
            write<uint8_t>(gServerConfig.bubbleDeath);
            write<uint8_t>(gServerConfig.headless);
            write<uint8_t>(gServerConfig.nametags);
            write<uint8_t>(gServerConfig.maxPlayers);
            write<uint8_t>(gServerConfig.pauseAnywhere);
            write<uint8_t>(0);
            
            // eeprom
            for (int i = 0; i < 512; i++) write<uint8_t>(255);
            
            sendBuffer();

            np->connected = true;
            np->globalIndex = globalIndex;
            np->name = name;
            np->modelIndex = model;
            memcpy(np->palette.colors, palette.colors, 24);

            packetInit(PACKET_NETWORK_PLAYERS, true, PLMT_NONE);
            write<uint8_t>(1);
            write<uint8_t>(np->type);
            write<uint8_t>(np->globalIndex);
            write<uint16_t>(np->currLevelAreaSeqId);
            write<int16_t>(np->currCourseNum);
            write<int16_t>(np->currActNum);
            write<int16_t>(np->currLevelNum);
            write<int16_t>(np->currAreaIndex);
            write<uint8_t>(np->currLevelSyncValid);
            write<uint8_t>(np->currAreaSyncValid);
            write<int64_t>(np->networkId);
            write<uint8_t>(np->modelIndex);
            for (int i = 0; i < 24; i++) {
                write<uint8_t>(np->palette.colors[i]);
            }
            write<std::string>(np->name, 64);
            write<std::string>(np->discordId, 64);

            sendBufferToAll();
            break;
        }
        case PACKET_NETWORK_PLAYERS_REQUEST: {
            std::cout << "Received network players request" << std::endl;

            uint8_t connectedCount = 0;
            for (const auto &player : gNetworkPlayers) {
                if (player.connected) {
                    connectedCount++;
                }
            }

            packetInit(PACKET_NETWORK_PLAYERS, true, PLMT_NONE);
            write<uint8_t>(connectedCount);
            for (const auto &player : gNetworkPlayers) {
                if (!player.connected || sockaddrInEqual(addr, gNetworkPlayerSockets[player.globalIndex])) continue;
                std::cout << "Sent player '" << player.name << "'  " << (int)player.globalIndex << std::endl;
                write<uint8_t>(player.type);
                write<uint8_t>(player.globalIndex);
                write<uint16_t>(player.currLevelAreaSeqId);
                write<int16_t>(player.currCourseNum);
                write<int16_t>(player.currActNum);
                write<int16_t>(player.currLevelNum);
                write<int16_t>(player.currAreaIndex);
                write<uint8_t>(player.currLevelSyncValid);
                write<uint8_t>(player.currAreaSyncValid);
                write<int64_t>(player.networkId);
                write<uint8_t>(player.modelIndex);
                for (int i = 0; i < 24; i++) {
                    write<uint8_t>(player.palette.colors[i]);
                }
                write<std::string>(player.name, MAX_CONFIG_STRING);
                write<std::string>(player.discordId, 64);
            }
            sendBuffer();
            break;
        }
        case PACKET_PING: {
            uint8_t globalIndex = read<uint8_t>();
            double timestamp = read<double>();

            packetInit(PACKET_PONG, false, PLMT_NONE);
            write<uint8_t>(globalIndex);
            write<double>(timestamp);

            std::cout << "Ping from id " << (int)globalIndex << std::endl;

            sendBuffer();
            break;
        }
        case PACKET_CHANGE_LEVEL: {
            int16_t courseNum = read<int16_t>();
            int16_t actNum = read<int16_t>();
            int16_t levelNum = read<int16_t>();
            int16_t areaIndex = read<int16_t>();

            NetworkPlayer *np = getNetworkPlayerFromAddr(addr);
            if (np) {
                std::cout << "Change Level from id " << (int)np->globalIndex << std::endl;
                np->currCourseNum = courseNum;
                np->currActNum = actNum;
                np->currLevelNum = levelNum;
                np->currAreaIndex = areaIndex;
                np->currLevelSyncValid = true;
                np->currAreaSyncValid = true;

                packetInit(PACKET_SYNC_VALID, true, PLMT_NONE);
                write<int16_t>(courseNum);
                write<int16_t>(actNum);
                write<int16_t>(levelNum);
                write<int16_t>(areaIndex);
                write<uint8_t>(0);
                write<uint8_t>(np->globalIndex);
                sendBuffer();
            }
            break;
        }
        case PACKET_LEVEL_AREA_INFORM: {
            uint16_t seq = read<uint16_t>();
            uint8_t globalIndex = read<uint8_t>();
            int16_t courseNum = read<int16_t>();
            int16_t actNum = read<int16_t>();
            int16_t levelNum = read<int16_t>();
            int16_t areaIndex = read<int16_t>();
            uint8_t levelSyncValid = read<uint8_t>();
            uint8_t areaSyncValid = read<uint8_t>();

            NetworkPlayer *np = &gNetworkPlayers[globalIndex];

            std::cout << "Area inform from id " << (int)globalIndex << std::endl;

            np->currLevelAreaSeqId = seq;
            np->currLevelSyncValid = levelSyncValid;
            np->currAreaSyncValid = areaSyncValid;

            np->currCourseNum = courseNum;
            np->currActNum = actNum;
            np->currLevelNum = levelNum;
            np->currAreaIndex = areaIndex;

            packetInit(PACKET_LEVEL_AREA_INFORM, true, PLMT_NONE);
            write<uint16_t>(seq);
            write<uint8_t>(globalIndex);
            write<int16_t>(courseNum);
            write<int16_t>(actNum);
            write<int16_t>(levelNum);
            write<int16_t>(areaIndex);
            write<uint8_t>(levelSyncValid);
            write<uint8_t>(areaSyncValid);
            sendBufferToAll();
            break;
        }
        case PACKET_CHAT: {
            uint8_t globalIndex = read<uint8_t>();
            uint16_t msgLen = read<uint16_t>();
            if (msgLen >= MAX_CHAT_MSG_LENGTH - 1) { msgLen = MAX_CHAT_MSG_LENGTH - 1; }
            std::string msg = read<std::string>(msgLen);
            std::cout << "Message from " << gNetworkPlayers[globalIndex].name << ": " << msg << std::endl;

            packetInit(PACKET_CHAT, true, PLMT_NONE);
            write<uint8_t>(globalIndex);
            write<uint16_t>(msgLen);
            write<std::string>(msg, msgLen);
            sendBufferToAll();
            break;
        }
        case PACKET_LEAVING: {
            uint8_t globalIndex = read<uint8_t>();
            packetInit(PACKET_LEAVING, true, PLMT_NONE);
            write<uint8_t>(globalIndex);
            sendBufferToAll();
            if (globalIndex < MAX_PLAYERS) {
                std::cout << "Player '" << gNetworkPlayers[globalIndex].name << "' " << "disconnected" << std::endl;
                gNetworkPlayers[globalIndex].connected = false;
                gNetworkPlayers[globalIndex].type = 0;
                gNetworkPlayers[globalIndex].globalIndex = 0;
                gNetworkPlayers[globalIndex].currLevelAreaSeqId = 0;
                gNetworkPlayers[globalIndex].currCourseNum = 0;
                gNetworkPlayers[globalIndex].currActNum = 0;
                gNetworkPlayers[globalIndex].currLevelNum = 0;
                gNetworkPlayers[globalIndex].currAreaIndex = 0;
                gNetworkPlayers[globalIndex].currLevelSyncValid = 0;
                gNetworkPlayers[globalIndex].currAreaSyncValid = 0;
                gNetworkPlayers[globalIndex].networkId = 0;
                gNetworkPlayers[globalIndex].modelIndex = 0;
                memset(gNetworkPlayers[globalIndex].palette.colors, 0x0, 24);
                gNetworkPlayers[globalIndex].name = "";
                gNetworkPlayers[globalIndex].discordId = "";
                memset(&gNetworkPlayerSockets[globalIndex], 0x0, sizeof(sockaddr_in));
            }
            break;
        }
        default: {
            std::cout << "Received packet type " << (int)pktType << " with flags " << (int)flags << std::endl;
            break;
        }
    }
}