#include <cstring>
#include <iostream>
#include <vector>
#include <zlib.h>
#include <cstdint>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#undef WIN32_LEAN_AND_MEAN
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "packet.hpp"

class UDPLobbyServer {
private:
    int port;
    int sock;

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
    UDPLobbyServer(int p) : port(p) {
#ifdef _WIN32
        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != 0) {
            std::cerr << "WSAStartup failed: " << res << std::endl;
            exit(1);
        }
#endif
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            std::cerr << "Failed to create socket." << std::endl;
            exit(1);
        }

        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    }

    void start() {
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Failed to bind to port " << port << std::endl;
            exit(1);
        }

        std::cout << "Headless lobby listening on UDP port " << port << std::endl;
        run_loop();
    }

    void run_loop() {
        uint8_t buffer[PACKET_LENGTH];
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (true) {
            ssize_t received = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);
            
            if (received > 0) {
                std::vector<uint8_t> decompressed = decompress_data(buffer, received);
                if (!decompressed.empty()) {
                    CoopPacket pkt(sock, client_addr, decompressed);
                    pkt.handle();
                }
            }
        }
    }

    ~UDPLobbyServer() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

int main() {
    gNetworkPlayers[0].globalIndex = 0;
    gNetworkPlayers[0].connected = true;
    gNetworkPlayers[0].name = "PeakServer";
    memset(gNetworkPlayers[0].palette.colors, 0xff, 24);
    UDPLobbyServer lobby(1282);
    lobby.start();
    return 0;
}