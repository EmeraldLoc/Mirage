#include "packet.hpp"
#include "network.hpp"
#include "config.hpp"
#include "savefile.hpp"
#include "log.hpp"
#include <algorithm>
#include <iostream>
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

// for reads
CoopPacket::CoopPacket(socket_t s, sockaddr_in a, const uint8_t *compData, size_t compLen) : sock(s), addr(a) {
    std::vector<uint8_t> dest(65536);
    uLongf destLen = dest.size();
    
    if (uncompress(dest.data(), &destLen, compData, compLen) != Z_OK) {
        Logging::log("SERVER", "Failed to decompress packet");
        return;
    }
    
    dest.resize(destLen);
    rawData = std::move(dest);

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

// for writes
CoopPacket::CoopPacket(socket_t s, sockaddr_in a, uint8_t pType, bool reliable, uint8_t levelMatchType, int asGlobalIndex) : sock(s), addr(a), pktType(pType), isReliable(reliable) {
    uint8_t initFlags = 0;
    if (levelMatchType == PLMT_AREA) initFlags |= (1 << 0);
    if (levelMatchType == PLMT_LEVEL) initFlags |= (1 << 3);
    if (sOrderedPackets) initFlags |= (1 << 2);

    if (reliable) {
        seqId = sNextSeqNum++;
        if (sNextSeqNum == 0) sNextSeqNum = 1;
    }

    write<uint8_t>(pType);
    write<uint16_t>(seqId);
    write<uint8_t>(initFlags);
    write<uint8_t>(PACKET_DESTINATION_BROADCAST);

    NetworkPlayer *localNp = &gNetworkPlayers[asGlobalIndex];
    if (sOrderedPackets) {
        uint8_t localGlobalIndex = localNp->globalIndex; 
        write<uint8_t>(localGlobalIndex); 
        write<uint16_t>(sCurrentOrderedGroupId); 
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

    isOrdered = sOrderedPackets;
    orderedGroupId = sOrderedPackets ? sCurrentOrderedGroupId : 0;
    orderedSeqId = 0;
}

CoopPacket CoopPacket::createOutgoing(socket_t s, sockaddr_in a, uint8_t pType, bool reliable, uint8_t levelMatchType, int asGlobalIndex) {
    return CoopPacket(s, a, pType, reliable, levelMatchType, asGlobalIndex);
}

void CoopPacket::setOrderedData() {
    if (orderedGroupId == 0) return;
    if (orderedSeqId != 0) return;
    
    orderedSeqId = sCurrentOrderedSeqId++;
    
    if (outBuffer.size() >= 10) {
        outBuffer[8] = orderedSeqId & 0xFF;
        outBuffer[9] = (orderedSeqId >> 8) & 0xFF;
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

void CoopPacket::handle() {
    if (seqId != 0 && pktType != PACKET_ACK) {
        auto ackPkt = CoopPacket::createOutgoing(sock, addr, PACKET_ACK, false, PLMT_NONE);
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

void CoopPacket::forwardPacket(uint8_t pType, bool reliable, uint8_t levelMatchType, int asGlobalIndex) {
    std::vector<uint8_t> payload(rawData.begin() + offset, rawData.end());

    auto outPkt = CoopPacket::createOutgoing(sock, addr, pType, reliable, levelMatchType, asGlobalIndex);
    for (uint8_t b : payload) {
        outPkt.write<uint8_t>(b);
    }
    outPkt.sendBufferToAll();
}

void CoopPacket::handleInternal() {
    uint8_t senderGlobalIndex = getNetworkPlayerFromAddr(addr) ? getNetworkPlayerFromAddr(addr)->globalIndex : 0;

    switch (pktType) {
        case PACKET_ACK: {
            uint16_t ackedSeq = read<uint16_t>();
            gReliablePackets.remove_if([this, ackedSeq](const ReliablePacket &p) {
                return p.seqId == ackedSeq && sockaddrInEqual(p.addr, this->addr);
            });
            break;
        }
        case PACKET_PLAYER:
            forwardPacket(PACKET_PLAYER, true, PLMT_AREA, senderGlobalIndex);
            break;
        case PACKET_PLAYER_SETTINGS:
            forwardPacket(PACKET_PLAYER_SETTINGS, true, PLMT_NONE, senderGlobalIndex);
            break;
        case PACKET_OBJECT:
            forwardPacket(PACKET_OBJECT, true, PLMT_AREA, senderGlobalIndex);
            break;
        case PACKET_COLLECT_COIN:
            forwardPacket(PACKET_COLLECT_COIN, true, PLMT_LEVEL, senderGlobalIndex);
            break;
        case PACKET_COLLECT_STAR:
            forwardPacket(PACKET_COLLECT_STAR, true, PLMT_NONE, senderGlobalIndex);
            break;
        case PACKET_COLLECT_ITEM:
            forwardPacket(PACKET_COLLECT_ITEM, true, PLMT_AREA, senderGlobalIndex);
            break;
        case PACKET_SPAWN_OBJECTS:
            forwardPacket(PACKET_SPAWN_OBJECTS, true, PLMT_AREA, senderGlobalIndex);
            break;
        case PACKET_SPAWN_STAR:
            forwardPacket(PACKET_SPAWN_STAR, true, PLMT_AREA, senderGlobalIndex);
            break;
        case PACKET_SPAWN_STAR_NLE:
            forwardPacket(PACKET_SPAWN_STAR_NLE, true, PLMT_AREA, senderGlobalIndex);
            break;
        /*case PACKET_AREA:
            forwardPacket(PACKET_AREA, true, PLMT_NONE, senderGlobalIndex);
            break;*/
        case PACKET_LUA_SYNC_TABLE:
            forwardPacket(PACKET_LUA_SYNC_TABLE, true, PLMT_NONE, senderGlobalIndex);
            break;
        case PACKET_LEVEL_RESPAWN_INFO:
            forwardPacket(PACKET_LEVEL_RESPAWN_INFO, true, PLMT_NONE);
            break;
        case PACKET_SAVE_FILE: {
            int32_t fileIndex = read<int32_t>();
            uint8_t backupSlot = read<uint8_t>();

            gSaveFile.save();
            break;
        }
        case PACKET_SAVE_SET_FLAG: {
            int32_t fileIndex = read<int32_t>();
            int32_t courseIndex = read<int32_t>();
            uint8_t courseData = read<uint8_t>();
            uint32_t flags = read<uint32_t>();
            uint8_t backupSlot = read<uint8_t>();

            gSaveFile.setFlags(flags, courseIndex, courseData);
            break;
        }
        case PACKET_SAVE_REMOVE_FLAG: {
            int32_t fileIndex = read<int32_t>();
            int32_t courseIndex = read<int32_t>();
            uint8_t courseData = read<uint8_t>();
            uint32_t flags = read<uint32_t>();
            uint8_t backupSlot = read<uint8_t>();

            gSaveFile.removeFlags(flags, courseIndex, courseData);
            break;
        }
        case PACKET_MOD_LIST_REQUEST: {
            std::string version = read<std::string>(128);
            Logging::log("SERVER", "Received mod list request:\n  Version: {} " , version);

            packetOrderedBegin();

            auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_MOD_LIST, true, PLMT_NONE);
            outPkt.write<std::string>(gServerConfig.version, 128);
            outPkt.write<uint16_t>(gServerConfig.mods.size());
            outPkt.sendBuffer();

            for (uint16_t i = 0; i < gServerConfig.mods.size(); i++) {
                CoopMod &mod = gServerConfig.mods[i];

                auto entryPkt = CoopPacket::createOutgoing(sock, addr, PACKET_MOD_LIST_ENTRY, true, PLMT_NONE);
                entryPkt.write<uint16_t>(i);
                uint16_t nameLen = mod.name.size();
                entryPkt.write<uint16_t>(nameLen);
                entryPkt.write<std::string>(mod.name, nameLen);
                entryPkt.write<uint16_t>(0);
                entryPkt.write<std::string>("", 0);
                uint16_t relLen = mod.relativePath.size();
                entryPkt.write<uint16_t>(relLen); 
                entryPkt.write<std::string>(mod.relativePath, relLen); 
                entryPkt.write<uint64_t>(mod.size); 
                entryPkt.write<uint8_t>(mod.isDirectory);
                entryPkt.write<uint8_t>(mod.pausable);
                entryPkt.write<uint8_t>(mod.ignoreScriptWarnings);
                entryPkt.write<uint16_t>(mod.files.size());
                entryPkt.sendBuffer();

                for (uint16_t j = 0; j < mod.files.size(); j++) {
                    CoopModFile &modFile = mod.files[j];
                    auto filePkt = CoopPacket::createOutgoing(sock, addr, PACKET_MOD_LIST_FILE, true, PLMT_NONE);
                    filePkt.write<uint16_t>(i);
                    filePkt.write<uint16_t>(j);
                    filePkt.write<uint16_t>(modFile.relativePath.size());
                    filePkt.write<std::string>(modFile.relativePath, modFile.relativePath.size());
                    filePkt.write<uint64_t>(modFile.size);
                    for (int h = 0; h < 16; h++) filePkt.write<uint8_t>(0); // hash
                    filePkt.sendBuffer();
                }
            }

            auto donePkt = CoopPacket::createOutgoing(sock, addr, PACKET_MOD_LIST_DONE, true, PLMT_NONE);
            donePkt.sendBuffer();

            packetOrderedEnd();
            break;
        }
        case PACKET_DOWNLOAD_REQUEST: {
            uint64_t requestOffset = read<uint64_t>();

            uint64_t totalModsSize = 0;
            for (auto &mod : gServerConfig.mods) {
                totalModsSize += mod.size;
            }

            for (uint64_t i = 0; i < 50; i++) {
                uint64_t sendOffset = requestOffset + (i * 800);
                if (sendOffset >= totalModsSize) break;

                uint8_t chunk[800] = { 0 };
                uint64_t chunkFill = 0;
                uint64_t fileStartOffset = 0;

                for (auto &mod : gServerConfig.mods) {
                    if ((fileStartOffset + mod.size) < sendOffset) {
                        fileStartOffset += mod.size;
                        continue;
                    }

                    for (auto &modFile : mod.files) {
                        if ((fileStartOffset + modFile.size) < sendOffset) {
                            fileStartOffset += modFile.size;
                            continue;
                        }

                        uint64_t fileReadOffset = (sendOffset > fileStartOffset) ? (sendOffset - fileStartOffset) : 0;
                        uint64_t fileReadLength = std::min((uint64_t)(modFile.size - fileReadOffset), (uint64_t)(800 - chunkFill));

                        std::ifstream file(modFile.realPath, std::ios::binary);
                        if (file.is_open()) {
                            file.seekg(fileReadOffset, std::ios::beg);
                            file.read(reinterpret_cast<char*>(&chunk[chunkFill]), fileReadLength);
                        } else {
                            Logging::log("SERVER", "Failed to open mod file for download: {} " , modFile.realPath);
                        }

                        chunkFill += fileReadLength;
                        fileStartOffset += modFile.size;

                        if (chunkFill >= 800) {
                            goto after_filled;
                        }
                    }
                }
            after_filled:
                auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_DOWNLOAD, true, PLMT_NONE);
                outPkt.write<uint64_t>(sendOffset);
                outPkt.write<uint64_t>(chunkFill);
                
                for (uint64_t j = 0; j < chunkFill; j++) {
                    outPkt.write<uint8_t>(chunk[j]);
                }

                outPkt.sendBuffer();
            }

            break;
        }
        case PACKET_JOIN_REQUEST: {
            bool exists = std::find_if(gNetworkPlayerSockets.begin(), gNetworkPlayerSockets.end(),
                [&](const sockaddr_in &address) {
                    return sockaddrInEqual(address, addr);
                }) != gNetworkPlayerSockets.end();
            if (exists) {
                Logging::log("SERVER", "Received join request from already joined socket, ignoring");
                break;
            }
            std::string version = read<std::string>(128);
            uint8_t model = read<uint8_t>();
            PlayerPalette palette;
            for (int i = 0; i < 24; i++) {
                palette.colors[i] = read<uint8_t>();
            }
            std::string name = read<std::string>(64);

            Logging::log("SERVER", "Received join request:\n  Version: {}\n  Name: {}", version, name);

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

            if (!globalIndex) {
                Logging::log("SERVER", "No available global indices, server full, dropping request from {}", name);
                break;
            }

            NetworkPlayer *np = &gNetworkPlayers[globalIndex];
            gNetworkPlayerSockets[globalIndex] = addr;

            auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_JOIN, true, PLMT_NONE);
            outPkt.write<std::string>(gServerConfig.version, 128);
            outPkt.write<uint8_t>(globalIndex);

            outPkt.write<int16_t>(gServerConfig.savefileIndex+1);
            outPkt.write<uint8_t>(gServerConfig.playerInteractions);
            outPkt.write<uint8_t>(gServerConfig.bouncyBounds);
            outPkt.write<uint8_t>(gServerConfig.knockStrength);
            outPkt.write<uint8_t>(gServerConfig.starStaying);
            outPkt.write<uint8_t>(gServerConfig.skipIntro);
            outPkt.write<uint8_t>(gServerConfig.bubbleDeath);
            outPkt.write<uint8_t>(gServerConfig.headless);
            outPkt.write<uint8_t>(gServerConfig.nametags);
            outPkt.write<uint8_t>(gServerConfig.maxPlayers);
            outPkt.write<uint8_t>(gServerConfig.pauseAnywhere);
            outPkt.write<uint8_t>(0);

            std::vector<uint8_t> eeprom = gSaveFile.getBuffer();
            for (int i = 0; i < 512; i++) outPkt.write<uint8_t>(eeprom[i]);
            outPkt.sendBuffer();

            np->connected = true;
            np->globalIndex = globalIndex;
            np->name = name;
            np->modelIndex = model;
            memcpy(np->palette.colors, palette.colors, 24);

            auto broadcastPkt = CoopPacket::createOutgoing(sock, addr, PACKET_NETWORK_PLAYERS, true, PLMT_NONE);
            broadcastPkt.write<uint8_t>(1);
            broadcastPkt.write<uint8_t>(np->type);
            broadcastPkt.write<uint8_t>(np->globalIndex);
            broadcastPkt.write<uint16_t>(np->currLevelAreaSeqId);
            broadcastPkt.write<int16_t>(np->currCourseNum);
            broadcastPkt.write<int16_t>(np->currActNum);
            broadcastPkt.write<int16_t>(np->currLevelNum);
            broadcastPkt.write<int16_t>(np->currAreaIndex);
            broadcastPkt.write<uint8_t>(np->currLevelSyncValid);
            broadcastPkt.write<uint8_t>(np->currAreaSyncValid);
            broadcastPkt.write<int64_t>(np->networkId);
            broadcastPkt.write<uint8_t>(np->modelIndex);
            for (int i = 0; i < 24; i++) {
                broadcastPkt.write<uint8_t>(np->palette.colors[i]);
            }
            broadcastPkt.write<std::string>(np->name, 64);
            broadcastPkt.write<std::string>(np->discordId, 64);
            broadcastPkt.sendBufferToAll();
            break;
        }
        case PACKET_NETWORK_PLAYERS_REQUEST: {
            Logging::log("SERVER", "Received network players request");

            uint8_t connectedCount = 0;
            for (const auto &player : gNetworkPlayers) {
                if (player.connected) {
                    connectedCount++;
                }
            }

            auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_NETWORK_PLAYERS, true, PLMT_NONE);
            outPkt.write<uint8_t>(connectedCount);
            for (const auto &player : gNetworkPlayers) {
                if (!player.connected || sockaddrInEqual(addr, gNetworkPlayerSockets[player.globalIndex])) continue;                
                outPkt.write<uint8_t>(player.type);
                outPkt.write<uint8_t>(player.globalIndex);
                outPkt.write<uint16_t>(player.currLevelAreaSeqId);
                outPkt.write<int16_t>(player.currCourseNum);
                outPkt.write<int16_t>(player.currActNum);
                outPkt.write<int16_t>(player.currLevelNum);
                outPkt.write<int16_t>(player.currAreaIndex);
                outPkt.write<uint8_t>(player.currLevelSyncValid);
                outPkt.write<uint8_t>(player.currAreaSyncValid);
                outPkt.write<int64_t>(player.networkId);
                outPkt.write<uint8_t>(player.modelIndex);
                for (int i = 0; i < 24; i++) {
                    outPkt.write<uint8_t>(player.palette.colors[i]);
                }
                outPkt.write<std::string>(player.name, MAX_CONFIG_STRING);
                outPkt.write<std::string>(player.discordId, 64);
            }
            outPkt.sendBuffer();
            break;
        }
        case PACKET_PING: {
            uint8_t globalIndex = read<uint8_t>();
            double timestamp = read<double>();

            auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_PONG, false, PLMT_NONE);
            outPkt.write<uint8_t>(globalIndex);
            outPkt.write<double>(timestamp);

            Logging::log("SERVER", "Received ping from {}", gNetworkPlayers[globalIndex].name);

            outPkt.sendBuffer();
            break;
        }
        case PACKET_CHANGE_LEVEL: {
            int16_t courseNum = read<int16_t>();
            int16_t actNum = read<int16_t>();
            int16_t levelNum = read<int16_t>();
            int16_t areaIndex = read<int16_t>();

            NetworkPlayer *np = getNetworkPlayerFromAddr(addr);
            if (np) {
                Logging::log("SERVER", "Received level change from {}", np->name);
                np->currCourseNum = courseNum;
                np->currActNum = actNum;
                np->currLevelNum = levelNum;
                np->currAreaIndex = areaIndex;
                np->currLevelSyncValid = true;
                np->currAreaSyncValid = true;

                auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_SYNC_VALID, true, PLMT_NONE);
                outPkt.write<int16_t>(courseNum);
                outPkt.write<int16_t>(actNum);
                outPkt.write<int16_t>(levelNum);
                outPkt.write<int16_t>(areaIndex);
                outPkt.write<uint8_t>(0);
                outPkt.write<uint8_t>(np->globalIndex);
                outPkt.sendBuffer();
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

            Logging::log("SERVER", "Received area inform from {}", np->name);

            np->currLevelAreaSeqId = seq;
            np->currLevelSyncValid = levelSyncValid;
            np->currAreaSyncValid = areaSyncValid;

            np->currCourseNum = courseNum;
            np->currActNum = actNum;
            np->currLevelNum = levelNum;
            np->currAreaIndex = areaIndex;

            auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_LEVEL_AREA_INFORM, true, PLMT_NONE);
            outPkt.write<uint16_t>(seq);
            outPkt.write<uint8_t>(globalIndex);
            outPkt.write<int16_t>(courseNum);
            outPkt.write<int16_t>(actNum);
            outPkt.write<int16_t>(levelNum);
            outPkt.write<int16_t>(areaIndex);
            outPkt.write<uint8_t>(levelSyncValid);
            outPkt.write<uint8_t>(areaSyncValid);
            outPkt.sendBufferToAll();
            break;
        }
        case PACKET_CHAT: {
            uint8_t globalIndex = read<uint8_t>();
            uint16_t msgLen = read<uint16_t>();
            if (msgLen >= MAX_CHAT_MSG_LENGTH - 1) { msgLen = MAX_CHAT_MSG_LENGTH - 1; }
            std::string msg = read<std::string>(msgLen);
            Logging::log("SERVER", "Received message from {}: {}", gNetworkPlayers[globalIndex].name, msg);

            auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_CHAT, true, PLMT_NONE);
            outPkt.write<uint8_t>(globalIndex);
            outPkt.write<uint16_t>(msgLen);
            outPkt.write<std::string>(msg, msgLen);
            outPkt.sendBufferToAll();
            break;
        }
        case PACKET_LEAVING: {
            uint8_t globalIndex = read<uint8_t>();
            auto outPkt = CoopPacket::createOutgoing(sock, addr, PACKET_LEAVING, true, PLMT_NONE);
            outPkt.write<uint8_t>(globalIndex);
            outPkt.sendBufferToAll();
            if (globalIndex < MAX_PLAYERS) {
                Logging::log("SERVER", "Player {} disconnected", gNetworkPlayers[globalIndex].name);
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
            Logging::log("SERVER", "Received unimplemented packet type {}", pktType);
            break;
        }
    }
}