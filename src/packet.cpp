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
    flags = read<uint8_t>();
    
    level_area_must_match = get_bit(flags, 0);
    request_broadcast = get_bit(flags, 1);
    is_ordered = get_bit(flags, 2);
    level_must_match = get_bit(flags, 3);

    dest_global_id = read<uint8_t>();

    if (is_ordered) {
        ordered_from_global_id = read<uint8_t>();
        ordered_group_id = read<uint16_t>();
        ordered_seq_id = read<uint16_t>();
    }

    if (level_area_must_match) {
        course_num = read<uint8_t>();
        act_num = read<uint8_t>();
        level_num = read<int16_t>();
        area_index = read<uint8_t>();
    } else if (level_must_match) {
        course_num = read<uint8_t>();
        act_num = read<uint8_t>();
        level_num = read<int16_t>();
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
    write<uint32_t>(hash_val);

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

    write<uint32_t>(hash_val);

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

    write<uint8_t>(p_type);
    write<uint16_t>(current_seq);
    write<uint8_t>(init_flags);
    write<uint8_t>(PACKET_DESTINATION_BROADCAST);

    NetworkPlayer *local_np = &gNetworkPlayers[0];
    if (sOrderedPackets) {
        uint8_t local_global_index = local_np->globalIndex; 
        write<uint8_t>(sOrderedPackets ? local_global_index : 0); 
        write<uint16_t>(sOrderedPackets ? sCurrentOrderedGroupId : 0); 
        write<uint16_t>(0);
    }
    
    if (level_match_type == PLMT_AREA) {
        write<uint8_t>(local_np->currCourseNum);
        write<uint8_t>(local_np->currActNum);
        write<int16_t>(local_np->currLevelNum);
        write<uint8_t>(local_np->currAreaIndex);
    } else if (level_match_type == PLMT_LEVEL) {
        write<uint8_t>(local_np->currCourseNum);
        write<uint8_t>(local_np->currActNum);
        write<int16_t>(local_np->currLevelNum);
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
        ack_pkt.write<uint16_t>(seq_id);
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

void CoopPacket::forward_and_ignore(uint8_t pkt_type, bool reliable, uint8_t level_match_type) {
    int32_t size = raw_data.size();
    std::vector<uint8_t> data(size);
    for (int i = 0; i < size; i++) {
        data[i] = read<uint8_t>();
    }

    packet_init(pkt_type, reliable, level_match_type);
    for (int i = 0; i < size; i++) {
        write<uint8_t>(data[i]);
    }
    send_buffer_to_all();
}

void CoopPacket::handle_internal() {
    switch (pkt_type) {
        case PACKET_ACK: {
            uint16_t acked_seq = read<uint16_t>();
            gReliablePackets.remove_if([this, acked_seq](const ReliablePacket &p) {
                return p.seq_id == acked_seq && sockaddr_in_equal(p.addr, this->addr);
            });
            break;
        }
        case PACKET_PLAYER:
            // todo: should be plmt area
            forward_and_ignore(PACKET_PLAYER, true, PLMT_NONE);
            break;
        case PACKET_OBJECT:
            // todo: should be plmt area
            forward_and_ignore(PACKET_OBJECT, true, PLMT_NONE);
            break;
        case PACKET_COLLECT_COIN:
            // todo: should be plmt level
            forward_and_ignore(PACKET_COLLECT_COIN, true, PLMT_NONE);
            break;
        case PACKET_LEVEL_RESPAWN_INFO:
            forward_and_ignore(PACKET_LEVEL_RESPAWN_INFO, true, PLMT_NONE);
            break;
        case PACKET_MOD_LIST_REQUEST: {
            std::string version = read<std::string>(128);
            std::cout << "Received mod list request:\n  Version: " << version.c_str() << std::endl;

            packet_ordered_begin();

            packet_init(PACKET_MOD_LIST, true, PLMT_NONE);
            write<std::string>(version, 128);
            write<uint16_t>(0);
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

            packet_init(PACKET_JOIN, true, PLMT_NONE);

            write<std::string>(version, 128);
            write<uint8_t>(globalIndex);

            write<int16_t>(1); // savefile num
            write<uint8_t>(1); // player interactions
            write<uint8_t>(0); // bouncy level bounds
            write<uint8_t>(0); // knock strength
            write<uint8_t>(0); // stay after star
            write<uint8_t>(1); // skip intro
            write<uint8_t>(0); // bubble
            write<uint8_t>(0); // headless
            write<uint8_t>(1); // nametags
            write<uint8_t>(16); // max players
            write<uint8_t>(0); // pause anywhere
            write<uint8_t>(0); // pvp type
            
            for (int i = 0; i < 512; i++) write<uint8_t>(0);
            
            send_buffer();

            np->connected = true;
            np->globalIndex = globalIndex;
            np->name = name;
            np->modelIndex = model;
            memcpy(np->palette.colors, palette.colors, 24);

            packet_init(PACKET_NETWORK_PLAYERS, true, PLMT_NONE);
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
            write<uint8_t>(connectedCount);
            for (const auto &player : gNetworkPlayers) {
                if (!player.connected || sockaddr_in_equal(addr, gNetworkPlayerSockets[player.globalIndex])) continue;
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
            send_buffer();
            break;
        }
        case PACKET_PING: {
            uint8_t globalIndex = read<uint8_t>();
            double timestamp = read<double>();

            packet_init(PACKET_PONG, false, PLMT_NONE);
            write<uint8_t>(globalIndex);
            write<double>(timestamp);

            std::cout << "Ping from id " << (int)globalIndex << std::endl;

            send_buffer();
            break;
        }
        case PACKET_CHANGE_LEVEL: {
            int16_t courseNum = read<int16_t>();
            int16_t actNum = read<int16_t>();
            int16_t levelNum = read<int16_t>();
            int16_t areaIndex = read<int16_t>();

            NetworkPlayer *np = get_network_player_from_addr(addr);
            if (np) {
                std::cout << "Change Level from id " << (int)np->globalIndex << std::endl;
                np->currCourseNum = courseNum;
                np->currActNum = actNum;
                np->currLevelNum = levelNum;
                np->currAreaIndex = areaIndex;
            }

            np->currLevelSyncValid = true;
            np->currAreaSyncValid = true;

            packet_init(PACKET_SYNC_VALID, true, PLMT_NONE);
            write<int16_t>(courseNum);
            write<int16_t>(actNum);
            write<int16_t>(levelNum);
            write<int16_t>(areaIndex);
            write<uint8_t>(0);
            write<uint8_t>(np->globalIndex);
            send_buffer();
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

            packet_init(PACKET_LEVEL_AREA_INFORM, true, PLMT_NONE);
            write<uint16_t>(seq);
            write<uint8_t>(globalIndex);
            write<int16_t>(courseNum);
            write<int16_t>(actNum);
            write<int16_t>(levelNum);
            write<int16_t>(areaIndex);
            write<uint8_t>(levelSyncValid);
            write<uint8_t>(areaSyncValid);
            send_buffer_to_all();
            break;
        }
        case PACKET_CHAT: {
            uint8_t globalIndex = read<uint8_t>();
            uint16_t msgLen = read<uint16_t>();
            if (msgLen >= MAX_CHAT_MSG_LENGTH - 1) { msgLen = MAX_CHAT_MSG_LENGTH - 1; }
            std::string msg = read<std::string>(msgLen);
            std::cout << "Message from " << gNetworkPlayers[globalIndex].name << ": " << msg << std::endl;

            packet_init(PACKET_CHAT, true, PLMT_NONE);
            write<uint8_t>(globalIndex);
            write<uint16_t>(msgLen);
            write<std::string>(msg, msgLen);
            send_buffer_to_all();
            break;
        }
        case PACKET_LEAVING: {
            uint8_t globalIndex = read<uint8_t>();
            packet_init(PACKET_LEAVING, true, PLMT_NONE);
            write<uint8_t>(globalIndex);
            send_buffer_to_all();
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
                std::cout << "Dropping reliable packet seq " << it->seq_id << ", max attempts reached\n";
                it = gReliablePackets.erase(it);
                continue;
            }
        }
        ++it;
    }
}