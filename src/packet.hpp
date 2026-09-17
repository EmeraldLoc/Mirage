#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <map>
#include <list>
#include "socket.hpp"

constexpr size_t PACKET_LENGTH = 3000;
constexpr uint8_t PACKET_DESTINATION_BROADCAST = ((uint8_t)-1);
constexpr uint8_t PACKET_DESTINATION_SERVER = ((uint8_t)-2);
constexpr uint8_t MAX_CONFIG_STRING = 64;
constexpr size_t MAX_CHAT_MSG_LENGTH = 500;

enum PacketType {
    PACKET_ACK,
    PACKET_PLAYER,
    PACKET_OBJECT,
    PACKET_SPAWN_OBJECTS,
    PACKET_SPAWN_STAR,
    PACKET_SPAWN_STAR_NLE,
    PACKET_COLLECT_STAR,
    PACKET_COLLECT_COIN,
    PACKET_COLLECT_ITEM,
    PACKET_GLOBAL_POPUP,
    PACKET_DEBUG_SYNC,
    PACKET_JOIN_REQUEST,
    PACKET_JOIN,
    PACKET_CHAT,
    PACKET_KICK,
    PACKET_KEEP_ALIVE,
    PACKET_LEAVING,
    PACKET_SAVE_FILE,
    PACKET_SAVE_SET_FLAG,
    PACKET_SAVE_REMOVE_FLAG,
    PACKET_NETWORK_PLAYERS,
    PACKET_DEATH,
    PACKET_PING,
    PACKET_PONG,
    PACKET_UNUSED_23,
    PACKET_CHANGE_LEVEL,
    PACKET_CHANGE_AREA,
    PACKET_LEVEL_AREA_REQUEST,
    PACKET_LEVEL_REQUEST,
    PACKET_LEVEL,
    PACKET_AREA_REQUEST,
    PACKET_AREA,
    PACKET_SYNC_VALID,
    PACKET_LEVEL_SPAWN_INFO,
    PACKET_LEVEL_MACRO,
    PACKET_LEVEL_AREA_INFORM,
    PACKET_LEVEL_RESPAWN_INFO,
    PACKET_CHANGE_WATER_LEVEL,
    PACKET_PLAYER_SETTINGS,
    PACKET_MOD_LIST_REQUEST,
    PACKET_MOD_LIST,
    PACKET_DOWNLOAD_REQUEST,
    PACKET_DOWNLOAD,
    PACKET_MOD_LIST_ENTRY,
    PACKET_MOD_LIST_FILE,
    PACKET_MOD_LIST_DONE,
    PACKET_LUA_SYNC_TABLE_REQUEST,
    PACKET_LUA_SYNC_TABLE,
    PACKET_NETWORK_PLAYERS_REQUEST,
    PACKET_REQUEST_FAILED,
    PACKET_LUA_CUSTOM,
    PACKET_LUA_CUSTOM_BYTESTRING,
    PACKET_COMMAND,
    PACKET_MODERATOR,
    PACKET_CUSTOM = 255,
};

enum PacketLevelMatchType {
    PLMT_NONE,
    PLMT_AREA,
    PLMT_LEVEL
};

class CoopPacket {
private:
    socket_t sock = 0;
    sockaddr_in addr{};
    uint64_t peerId = 0;
    std::vector<uint8_t> rawData;
    std::vector<uint8_t> outBuffer;
    size_t offset = 3;

    CoopPacket(socket_t s, sockaddr_in a, uint64_t pId, uint8_t pType, bool reliable, uint8_t levelMatchType, int asGlobalIndex);

    std::vector<uint8_t> compressAndHash();
    void execute();
    void processOrdered();
    void setOrderedData();
public:
    uint8_t pktType = 0;
    uint16_t seqId = 0;
    bool isReliable = false;
    uint8_t flags = 0;
    bool levelAreaMustMatch = false;
    bool requestBroadcast = false;
    bool isOrdered = false;
    bool levelMustMatch = false;
    uint8_t destGlobalId = 0;

    uint8_t orderedFromGlobalId = 0;
    uint16_t orderedGroupId = 0;
    uint16_t orderedSeqId = 0;

    uint8_t courseNum = 0;
    uint8_t actNum = 0;
    int16_t levelNum = 0;
    uint8_t areaIndex = 0;

    CoopPacket(socket_t s, sockaddr_in a, uint64_t pId, const uint8_t *compData, size_t compLen);
    static CoopPacket createOutgoing(socket_t s, sockaddr_in a, uint64_t pId, uint8_t pType, bool reliable = false, uint8_t levelMatchType = PLMT_NONE, int asGlobalIndex = 0);
    
    CoopPacket duplicate();

    template<typename T>
    T read(size_t length = 0) {
        if constexpr(std::is_same_v<T, std::string>) {
            if (offset >= rawData.size()) return "";
            size_t available = rawData.size() - offset;
            size_t actualLen = std::min(length, available);
            std::string val(reinterpret_cast<const char*>(rawData.data() + offset), actualLen);
            offset += std::min(length, available);
            return val;
        } else {
            constexpr size_t sz = sizeof(T);
            if (offset + sz > rawData.size()) {
                offset = rawData.size();
                return T(0);
            }
            T val{};
            std::memcpy(&val, rawData.data() + offset, sz);
            offset += sz;
            return val;
        }
    }

    template<typename T>
    void write(const T &val, size_t length = 0) {
        if constexpr(std::is_same_v<T, std::string>) {
            size_t strLen = val.length();
            outBuffer.reserve(outBuffer.size() + length);
            for (size_t i = 0; i < length; ++i) {
                if (i < strLen) outBuffer.push_back(static_cast<uint8_t>(val[i]));
                else outBuffer.push_back(0x00);
            }
        } else {
            constexpr size_t sz = sizeof(T);
            outBuffer.reserve(outBuffer.size() + sz);
            const uint8_t *ptr = reinterpret_cast<const uint8_t*>(&val);
            for (size_t i = 0; i < sz; ++i) {
                outBuffer.push_back(ptr[i]);
            }
        }
    }

    void sendTo(sockaddr_in dest, uint64_t destPeerId);
    void sendTo(int globalIdx);
    void sendBack();
    void sendToAll();
    void handle();
};

struct ReliablePacket {
    uint16_t seqId;
    socket_t sock;
    sockaddr_in addr;
    uint64_t peerId;
    std::vector<uint8_t> compressedData;
    std::chrono::steady_clock::time_point lastSend;
    int sendAttempts;
};

struct OrderedState {
    uint16_t processSeqId = 1;
    std::map<uint16_t, CoopPacket> queuedPackets;
};

extern std::list<ReliablePacket> gReliablePackets;

extern void packetOrderedBegin();
extern void packetOrderedEnd();