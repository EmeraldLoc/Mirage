#include <cstring>
#include <iostream>
#include <vector>
#include <zlib.h>
#include <cstdint>
#include "packet.hpp"
#include "network_player.hpp"
#include "socket.hpp"

class CoopLobby {
private:
    int port;
    UDPSocket udp_socket;

    std::vector<uint8_t> decompress_data(const uint8_t *data, size_t len) {
        std::vector<uint8_t> dest(65536);
        uLongf destLen = dest.size();
        if (uncompress(dest.data(), &destLen, data, len) == Z_OK) {
            dest.resize(destLen);
            return dest;
        }
        return {};
    }
public:
    CoopLobby(int p) : port(p), udp_socket(p) {}

    void start() {
        std::cout << "Headless lobby listening on UDP port " << port << std::endl;
        run_loop();
    }

    void run_loop() {
        uint8_t buffer[PACKET_LENGTH];
        sockaddr_in client_addr;

        while (true) {
            ssize_t received = udp_socket.receive(buffer, sizeof(buffer), client_addr);
            
            if (received > 0) {
                std::vector<uint8_t> decompressed = decompress_data(buffer, received);
                if (!decompressed.empty()) {
                    CoopPacket pkt(udp_socket.get_sock(), client_addr, decompressed);
                    pkt.handle();
                }
            }

            update_network_reliables();
        }
    }
};

int main() {
    gNetworkPlayers[0].type = 2;
    gNetworkPlayers[0].globalIndex = 0;
    gNetworkPlayers[0].connected = true;
    gNetworkPlayers[0].name = "PeakServer";
    gNetworkPlayers[0].currLevelNum = 16;
    gNetworkPlayers[0].currAreaIndex = 1;
    //gNetworkPlayers[0].currAreaSyncValid = true;
    //gNetworkPlayers[0].currLevelSyncValid = true;
    memset(gNetworkPlayers[0].palette.colors, 0xff, 24);
    
    CoopLobby lobby(1282);
    lobby.start();
    
    return 0;
}