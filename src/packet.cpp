#include "packet.hpp"
#include "network_player.hpp"
#include <algorithm>
#include <ostream>
#include <zlib.h>
#include <cstdint>
#include <list>

static std::list<ReliablePacket> gReliablePackets;
static std::map<std::pair<uint8_t, uint16_t>, OrderedState> gOrderedStates;

static bool sOrderedPackets = false;
static uint16_t sCurrentOrderedGroupId = 0;
static uint16_t sCurrentOrderedSeqId = 0;
static uint16_t sNextSeqNum = 1;

inline bool get_bit(uint8_t val, uint8_t num) {
    return (val >> num) & 1;
}

void packet_ordered_begin() {
    if (sOrderedPackets) return;
    sOrderedPackets = true;

    sCurrentOrderedGroupId++;
    if (sCurrentOrderedGroupId == 0) sCurrentOrderedGroupId++;
    sCurrentOrderedSeqId = 1;
}

void packet_ordered_end() {
    sOrderedPackets = false;
    sCurrentOrderedSeqId = 0;
}

void CoopPacket::set_ordered_data() {
    if (this->ordered_group_id == 0) return;
    if (this->ordered_seq_id != 0) return;
    
    this->ordered_seq_id = sCurrentOrderedSeqId++;
    
    if (out_buffer.size() >= 10) {
        out_buffer[8] = this->ordered_seq_id & 0xFF;
        out_buffer[9] = (this->ordered_seq_id >> 8) & 0xFF;
    }
}

CoopPacket::CoopPacket(socket_t s, sockaddr_in a, const std::vector<uint8_t> &data) : sock(s), addr(a), raw_data(data) {
    if (raw_data.size() < 3) return;

    pkt_type = raw_data[0];
    seq_id = raw_data[1] | (raw_data[2] << 8);
    offset = 3;
    flags = read_u8();
    
    level_area_must_match = get_bit(flags, 0);
    request_broadcast = get_bit(flags, 1);
    is_ordered = get_bit(flags, 2);
    level_must_match = get_bit(flags, 3);

    dest_global_id = read_u8();

    if (is_ordered) {
        ordered_from_global_id = read_u8();
        ordered_group_id = read_u16();
        ordered_seq_id = read_u16();
    }

    if (level_area_must_match) {
        course_num = read_u8();
        act_num = read_u8();
        level_num = read_s16();
        area_index = read_u8();
    } else if (level_must_match) {
        course_num = read_u8();
        act_num = read_u8();
        level_num = read_s16();
    }
}

uint8_t CoopPacket::read_u8() { 
    if (offset >= raw_data.size()) return 0;
    return raw_data[offset++]; 
}

uint16_t CoopPacket::read_u16() {
    if (offset + 2 > raw_data.size()) { offset = raw_data.size(); return 0; }
    uint16_t val = raw_data[offset] | (raw_data[offset+1] << 8);
    offset += 2;
    return val;
}

int16_t CoopPacket::read_s16() {
    return (int16_t)read_u16();
}

uint32_t CoopPacket::read_u32() {
    if (offset + 4 > raw_data.size()) { offset = raw_data.size(); return 0; }
    uint32_t val = raw_data[offset] | (raw_data[offset+1] << 8) | (raw_data[offset+2] << 16) | (raw_data[offset+3] << 24);
    offset += 4;
    return val;
}

uint64_t CoopPacket::read_u64() {
    if (offset + 8 > raw_data.size()) { offset = raw_data.size(); return 0; }
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= ((uint64_t)raw_data[offset+i] << (8 * i));
    }
    offset += 8;
    return val;
}

int64_t CoopPacket::read_s64() {
    return (int64_t)read_u64();
}

double CoopPacket::read_f64() {
    uint64_t bits = read_u64();
    double val;
    memcpy(&val, &bits, sizeof(val));
    return val;
}

float CoopPacket::read_f32() {
    uint32_t bits = read_u32();
    float val;
    memcpy(&val, &bits, sizeof(val));
    return val;
}

std::string CoopPacket::read_str(size_t length) {
    if (offset >= raw_data.size()) return "";
    size_t actual_len = std::min(length, raw_data.size() - offset);
    std::string val(raw_data.begin() + offset, raw_data.begin() + offset + actual_len);
    offset += length;
    if (offset > raw_data.size()) offset = raw_data.size();
    return val;
}

void CoopPacket::write_u8(uint8_t val) { 
    out_buffer.push_back(val); 
}

void CoopPacket::write_u16(uint16_t val) {
    out_buffer.push_back(val & 0xFF);
    out_buffer.push_back((val >> 8) & 0xFF);
}

void CoopPacket::write_s16(int16_t val) { 
    write_u16((uint16_t)(val)); 
}

void CoopPacket::write_u32(uint32_t val) {
    out_buffer.push_back(val & 0xFF);
    out_buffer.push_back((val >> 8) & 0xFF);
    out_buffer.push_back((val >> 16) & 0xFF);
    out_buffer.push_back((val >> 24) & 0xFF);
}

void CoopPacket::write_u64(uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        out_buffer.push_back((val >> (8 * i)) & 0xFF);
    }
}

void CoopPacket::write_s64(int64_t val) {
    for (int i = 0; i < 8; ++i) {
        out_buffer.push_back((val >> (8 * i)) & 0xFF);
    }
}

void CoopPacket::write_f64(double val) {
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));
    write_u64(bits);
}

void CoopPacket::write_f32(float val) {
    uint32_t bits;
    memcpy(&bits, &val, sizeof(bits));
    write_u32(bits);
}

void CoopPacket::write_str(const std::string &text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (i < text.length()) out_buffer.push_back(text[i]);
        else out_buffer.push_back(0x00);
    }
}

void CoopPacket::send_buffer() {
    if (out_buffer.empty()) return;

    if (this->is_ordered) {
        set_ordered_data();
    }

    uint32_t hash_val = 0;
    int byte_pos = 0;
    for (uint8_t b : out_buffer) {
        hash_val ^= ((uint32_t)(b) << (8 * byte_pos));
        byte_pos = (byte_pos + 1) % 4;
    }
    write_u32(hash_val);

    std::vector<uint8_t> compressed(compressBound(out_buffer.size()));
    uLongf destLen = compressed.size();
    if (compress2(compressed.data(), &destLen, out_buffer.data(), out_buffer.size(), Z_BEST_COMPRESSION) == Z_OK) {
        compressed.resize(destLen);
        sendto(sock, reinterpret_cast<const char*>(compressed.data()), compressed.size(), 0, (struct sockaddr*)&addr, sizeof(addr));

        if (this->is_reliable && this->seq_id != 0) {
            gReliablePackets.push_back({
                this->seq_id,
                this->sock,
                this->addr,
                compressed,
                std::chrono::steady_clock::now(),
                1
            });
        }
    }
    out_buffer.clear();
}

void CoopPacket::send_buffer_to_all() {
    if (out_buffer.empty()) return;

    if (this->is_ordered) {
        set_ordered_data();
    }

    uint32_t hash_val = 0;
    int byte_pos = 0;

    for (uint8_t b : out_buffer) {
        hash_val ^= ((uint32_t)b << (8 * byte_pos));
        byte_pos = (byte_pos + 1) % 4;
    }

    write_u32(hash_val);

    std::vector<uint8_t> compressed(compressBound(out_buffer.size()));
    uLongf destLen = compressed.size();
    if (compress(compressed.data(), &destLen, out_buffer.data(), out_buffer.size()) == Z_OK) {
        compressed.resize(destLen);

        for (int i = 1; i < MAX_PLAYERS; i++) {
            if (!gNetworkPlayers[i].connected || sockaddr_in_equal(addr, gNetworkPlayerSockets[i])) continue;

            sendto(sock, reinterpret_cast<const char*>(compressed.data()), compressed.size(), 0, (struct sockaddr*)&gNetworkPlayerSockets[i], sizeof(gNetworkPlayerSockets[i]));

            if (this->is_reliable && this->seq_id != 0) {
                gReliablePackets.push_back({
                    this->seq_id,
                    this->sock,
                    gNetworkPlayerSockets[i],
                    compressed,
                    std::chrono::steady_clock::now(),
                    1
                });
            }
        }
    }

    out_buffer.clear();
}

void CoopPacket::packet_init(uint8_t p_type, bool reliable, uint8_t level_match_type) {
    out_buffer.clear();
    uint8_t init_flags = 0;
    
    if (level_match_type == PLMT_AREA) init_flags |= (1 << 0);
    if (level_match_type == PLMT_LEVEL) init_flags |= (1 << 3);
    if (sOrderedPackets) init_flags |= (1 << 2);

    uint16_t current_seq = 0;
    if (reliable) {
        current_seq = sNextSeqNum++;
        if (sNextSeqNum == 0) sNextSeqNum = 1;
    }

    write_u8(p_type);
    write_u16(current_seq);
    write_u8(init_flags);
    write_u8(PACKET_DESTINATION_BROADCAST);

    NetworkPlayer *local_np = &gNetworkPlayers[0];
    if (sOrderedPackets) {
        uint8_t local_global_index = local_np->globalIndex; 
        write_u8(sOrderedPackets ? local_global_index : 0); 
        write_u16(sOrderedPackets ? sCurrentOrderedGroupId : 0); 
        write_u16(0);
    }
    
    if (level_match_type == PLMT_AREA) {
        write_u8(local_np->currCourseNum);
        write_u8(local_np->currActNum);
        write_s16(local_np->currLevelNum);
        write_u8(local_np->currAreaIndex);
    } else if (level_match_type == PLMT_LEVEL) {
        write_u8(local_np->currCourseNum);
        write_u8(local_np->currActNum);
        write_s16(local_np->currLevelNum);
    }

    this->is_reliable = reliable;
    this->seq_id = current_seq;
    this->is_ordered = (sOrderedPackets);
    this->ordered_group_id = sOrderedPackets ? sCurrentOrderedGroupId : 0;
    this->ordered_seq_id = 0;
}

void CoopPacket::handle() {
    if (seq_id != 0 && pkt_type != PACKET_ACK) {
        CoopPacket ack_pkt(sock, addr, {});
        ack_pkt.packet_init(PACKET_ACK, false, PLMT_NONE);
        ack_pkt.write_u16(seq_id);
        ack_pkt.send_buffer();
    }

    if (is_ordered) {
        process_ordered_and_handle();
    } else {
        handle_internal();
    }
}

void CoopPacket::process_ordered_and_handle() {
    std::pair<uint8_t, uint16_t> key = {ordered_from_global_id, ordered_group_id};
    auto &state = gOrderedStates[key];

    if (ordered_seq_id < state.process_seq_id) {
        return;
    }

    state.queued_packets.emplace(ordered_seq_id, *this);

    auto it = state.queued_packets.begin();
    while (it != state.queued_packets.end() && it->first == state.process_seq_id) {
        it->second.handle_internal();
        it = state.queued_packets.erase(it);
        state.process_seq_id++;
    }
}

void CoopPacket::handle_internal() {
    switch (pkt_type) {
        case PACKET_ACK: {
            uint16_t acked_seq = read_u16();
            gReliablePackets.remove_if([this, acked_seq](const ReliablePacket& p) {
                return p.seq_id == acked_seq && sockaddr_in_equal(p.addr, this->addr);
            });
            break;
        }
        case PACKET_MOD_LIST_REQUEST: {
            std::string version = read_str(128);
            std::cout << "Received mod list request:\n  Version: " << version.c_str() << std::endl;

            packet_ordered_begin();

            packet_init(PACKET_MOD_LIST, true, PLMT_NONE);
            write_str(version, 128);
            write_u16(0); // mod count
            send_buffer();

            packet_init(PACKET_MOD_LIST_DONE, true, PLMT_NONE);

            send_buffer();

            packet_ordered_end();
            break;
        }
        case PACKET_JOIN_REQUEST: {
            bool exists = std::find_if(gNetworkPlayerSockets.begin(), gNetworkPlayerSockets.end(),
                [&](const sockaddr_in &address) {
                    return sockaddr_in_equal(address, addr);
                }) != gNetworkPlayerSockets.end();
            if (exists) {
                std::cout << "Received join request from already joined socket, ignoring" << std::endl;
                break;
            }
            std::string version = read_str(128);
            uint8_t model = read_u8();
            PlayerPalette palette;
            for (int i = 0; i < 24; i++) {
                palette.colors[i] = read_u8();
            }
            std::string name = read_str(64);

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

            packet_init(PACKET_JOIN, true, PLMT_NONE);

            write_str(version, 128);
            write_u8(globalIndex);

            write_s16(1);  // savefile num
            write_u8(1);   // player interactions
            write_u8(0);   // bouncy level bounds
            write_u8(0);   // knock strength
            write_u8(0);   // stay after star
            write_u8(1);   // skip intro
            write_u8(0);   // bubble
            write_u8(0);   // headless
            write_u8(1);   // nametags
            write_u8(16);  // max players
            write_u8(0);   // pause anywhere
            write_u8(0);   // pvp type
            
            for (int i = 0; i < 512; i++) write_u8(0);
            
            send_buffer();

            np->connected = true;
            //np->type = 3;
            np->globalIndex = globalIndex;
            np->name = name;
            np->modelIndex = model;
            memcpy(np->palette.colors, palette.colors, 24);

            connectedCount = 0;
            for (const auto &player : gNetworkPlayers) {
                if (player.connected) {
                    connectedCount++;
                }
            }

            packet_init(PACKET_NETWORK_PLAYERS, true, PLMT_NONE);
            write_u8(connectedCount);
            write_u8(np->type);
            write_u8(np->globalIndex);
            write_u16(np->currLevelAreaSeqId);
            write_s16(np->currCourseNum);
            write_s16(np->currActNum);
            write_s16(np->currLevelNum);
            write_s16(np->currAreaIndex);
            write_u8(np->currLevelSyncValid);
            write_u8(np->currAreaSyncValid);
            write_s64(np->networkId);
            write_u8(np->modelIndex);
            for (int i = 0; i < 24; i++) {
                write_u8(np->palette.colors[i]);
            }
            write_str(np->name, 64);
            write_str(np->discordId, 64);

            send_buffer_to_all();
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

            packet_init(PACKET_NETWORK_PLAYERS, true, PLMT_NONE);
            write_u8(connectedCount);
            for (const auto &player : gNetworkPlayers) {
                if (!player.connected || sockaddr_in_equal(addr, gNetworkPlayerSockets[player.globalIndex])) continue;
                std::cout << "Sent player '" << player.name << "'  " << (int)player.globalIndex << std::endl;
                write_u8(player.type);
                write_u8(player.globalIndex);
                write_u16(player.currLevelAreaSeqId);
                write_s16(player.currCourseNum);
                write_s16(player.currActNum);
                write_s16(player.currLevelNum);
                write_s16(player.currAreaIndex);
                write_u8(player.currLevelSyncValid);
                write_u8(player.currAreaSyncValid);
                write_s64(player.networkId);
                write_u8(player.modelIndex);
                for (int i = 0; i < 24; i++) {
                    write_u8(player.palette.colors[i]);
                }
                write_str(player.name, MAX_CONFIG_STRING);
                write_str(player.discordId, 64);
            }
            send_buffer();
            break;
        }
        case PACKET_PING: {
            uint8_t globalIndex = read_u8();
            double timestamp = read_f64();

            packet_init(PACKET_PONG, false, PLMT_NONE);
            write_u8(globalIndex);
            write_f64(timestamp);

            std::cout << "Ping from id " << (int)globalIndex << std::endl;

            send_buffer();
            break;
        }
        case PACKET_CHANGE_LEVEL: {
            int16_t courseNum = read_s16();
            int16_t actNum = read_s16();
            int16_t levelNum = read_s16();
            int16_t areaIndex = read_s16();

            NetworkPlayer *np = get_network_player_from_addr(addr);
            if (np) {
                std::cout << "Change Level from id " << (int)np->globalIndex << std::endl;
                np->currCourseNum = courseNum;
                np->currActNum = actNum;
                np->currLevelNum = levelNum;
                np->currAreaIndex = areaIndex;
            }
            break;
        }
        case PACKET_LEVEL_AREA_INFORM: {
            uint16_t seq = read_u16();
            uint8_t globalIndex = read_u8();
            int16_t courseNum = read_s16();
            int16_t actNum = read_s16();
            int16_t levelNum = read_s16();
            int16_t areaIndex = read_s16();
            uint8_t levelSyncValid = read_u8();
            uint8_t areaSyncValid = read_u8();

            NetworkPlayer *np = &gNetworkPlayers[globalIndex];

            std::cout << "Area inform from id " << (int)globalIndex << std::endl;

            np->currLevelAreaSeqId = seq;
            np->currLevelSyncValid = levelSyncValid;
            np->currAreaSyncValid = areaSyncValid;

            np->currCourseNum = courseNum;
            np->currActNum = actNum;
            np->currLevelNum = levelNum;
            np->currAreaIndex = areaIndex;

            packet_init(PACKET_LEVEL_AREA_INFORM, true, PLMT_NONE);
            write_u16(seq);
            write_u8(globalIndex);
            write_s16(courseNum);
            write_s16(actNum);
            write_s16(levelNum);
            write_s16(areaIndex);
            write_u8(levelSyncValid);
            write_u8(areaSyncValid);
            send_buffer_to_all();
            break;
        }
        case PACKET_CHAT: {
            uint8_t globalIndex = read_u8();
            uint16_t msgLen = read_u16();
            if (msgLen >= MAX_CHAT_MSG_LENGTH - 1) { msgLen = MAX_CHAT_MSG_LENGTH - 1; }
            std::string msg = read_str(msgLen);
            std::cout << "Message from " << gNetworkPlayers[globalIndex].name << ": " << msg << std::endl;

            packet_init(PACKET_CHAT, true, PLMT_NONE);
            write_u8(globalIndex);
            write_u16(msgLen);
            write_str(msg, msgLen);
            send_buffer_to_all();
            break;
        }
        case PACKET_LEAVING: {
            uint8_t globalIndex = read_u8();
            packet_init(PACKET_LEAVING, true, PLMT_NONE);
            write_u8(globalIndex);
            send_buffer_to_all();
            if (globalIndex < MAX_PLAYERS) {
                std::cout << "Player disconnected: " << (int)globalIndex << std::endl;
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
            std::cout << "Received packet type " << (int)pkt_type << " with flags " << (int)flags << std::endl;
            break;
        }
    }
}

void update_network_reliables() {
    auto now = std::chrono::steady_clock::now();
    auto it = gReliablePackets.begin();

    while (it != gReliablePackets.end()) {
        std::chrono::duration<float> elapsed = now - it->last_send;
        float max_elapsed = std::min(0.07f * (it->send_attempts * it->send_attempts), 4.0f);

        if (elapsed.count() > max_elapsed) {
            sendto(it->sock, reinterpret_cast<const char*>(it->compressed_data.data()), it->compressed_data.size(), 0, (struct sockaddr*)&it->addr, sizeof(it->addr));

            it->last_send = now;
            it->send_attempts++;

            if (it->send_attempts >= 15) {
                std::cout << "Dropping reliable packet seq " << it->seq_id << ", max attempts reached.\n";
                it = gReliablePackets.erase(it);
                continue;
            }
        }
        ++it;
    }
}