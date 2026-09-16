#include "server.hpp"
#include "packet.hpp"
#include "network.hpp"
#include "log.hpp"
#include "config.hpp"
#include <thread>
#include <chrono>

CoopServer::CoopServer(int p) : port(p) {}

void CoopServer::runLoop() {
    Logging::log("SERVER", "Starting server on port {}", port);

    NetworkSystemType type = (gServerConfig.networkSystem == 1) ? SYS_COOPNET : SYS_SOCKET;
    if (!networkInit(type, port)) {
        Logging::log("SERVER", "Failed to initialize network system!");
        return;
    }

    while (true) {
        updateNetwork();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    networkShutdown();
}