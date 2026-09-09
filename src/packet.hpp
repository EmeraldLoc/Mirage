#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <array>
#include <cstdint>
#include <chrono>
#include <map>
#include "socket.hpp"

#define PACKET_LENGTH 3000
#define PACKET_DESTINATION_BROADCAST ((uint8_t)-1)
#define PACKET_DESTINATION_SERVER ((uint8_t)-2)
#define MAX_CONFIG_STRING 64
#define MAX_CHAT_MSG_LENGTH 500

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
    socket_t sock;
    sockaddr_in addr;
    std::vector<uint8_t> raw_data;
    std::vector<uint8_t> out_buffer;
    size_t offset = 3;
public:
    uint8_t pkt_type;
    uint16_t seq_id = 0;
    bool is_reliable = false;
    uint8_t flags;
    bool level_area_must_match;
    bool request_broadcast;
    bool is_ordered;
    bool level_must_match;
    uint8_t dest_global_id;

    uint8_t ordered_from_global_id = 0;
    uint16_t ordered_group_id = 0;
    uint16_t ordered_seq_id = 0;

    uint8_t course_num = 0;
    uint8_t act_num = 0;
    int16_t level_num = 0;
    uint8_t area_index = 0;

    CoopPacket(socket_t s, sockaddr_in a, const std::vector<uint8_t> &data);

    template<typename T>
    T read(size_t length = 0) {
        if constexpr(std::is_same_v<T, std::string>) {
            if (offset >= raw_data.size()) return "";
            size_t actual_len = std::min(length, raw_data.size() - offset);
            std::string val(raw_data.begin() + offset, raw_data.begin() + offset + actual_len);
            offset += length;
            if (offset > raw_data.size()) offset = raw_data.size();
            return val;
        } else {
            constexpr size_t sz = sizeof(T);
            if (offset + sz > raw_data.size()) {
                offset = raw_data.size();
                return T(0);
            }
            uint64_t raw_val = 0;
            for (size_t i = 0; i < sz; ++i) {
                raw_val |= ((uint64_t)raw_data[offset + i] << (8 * i));
            }
            offset += sz;

            if constexpr(std::is_floating_point_v<T>) {
                T val;
                std::memcpy(&val, &raw_val, sz);
                return val;
            } else {
                return static_cast<T>(raw_val);
            }
        }
    }

    template<typename T>
    void write(const T &val, size_t length = 0) {
        if constexpr(std::is_same_v<T, std::string>) {
            for (size_t i = 0; i < length; ++i) {
                if (i < val.length()) out_buffer.push_back(val[i]);
                else out_buffer.push_back(0x00);
            }
        } else {
            constexpr size_t sz = sizeof(T);
            uint64_t raw_val = 0;
            if constexpr(std::is_floating_point_v<T>) {
                std::memcpy(&raw_val, &val, sz);
            } else {
                raw_val = static_cast<uint64_t>(val);
            }
            for (size_t i = 0; i < sz; ++i) {
                out_buffer.push_back((raw_val >> (8 * i)) & 0xFF);
            }
        }
    }

    void send_buffer();
    void send_buffer_to_all();
    void packet_init(uint8_t p_type, bool reliable = false, uint8_t level_match_type = PLMT_NONE);
    
    void set_ordered_data();
    void handle();
    void forward_and_ignore(uint8_t pkt_type, bool reliable=true, uint8_t level_match_type=PLMT_NONE);
    void handle_internal();
    void process_ordered_and_handle();
};

struct ReliablePacket {
    uint16_t seq_id;
    socket_t sock;
    sockaddr_in addr;
    std::vector<uint8_t> compressed_data;
    std::chrono::steady_clock::time_point last_send;
    int send_attempts;
};

struct OrderedState {
    uint16_t process_seq_id = 1;
    std::map<uint16_t, CoopPacket> queued_packets;
};

void update_network_reliables();