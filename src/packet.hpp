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

    uint8_t read_u8();
    uint16_t read_u16();
    int16_t read_s16();
    uint32_t read_u32();
    uint64_t read_u64();
    int64_t read_s64();
    double read_f64();
    float read_f32();
    std::string read_str(size_t length);

    void write_u8(uint8_t val);
    void write_u16(uint16_t val);
    void write_s16(int16_t val);
    void write_u32(uint32_t val);
    void write_u64(uint64_t val);
    void write_s64(int64_t val);
    void write_f64(double val);
    void write_f32(float val);
    void write_str(const std::string &text, size_t length);

    void send_buffer();
    void send_buffer_to_all();
    void packet_init(uint8_t p_type, bool reliable = false, uint8_t level_match_type = PLMT_NONE);
    
    void set_ordered_data();
    void handle();
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