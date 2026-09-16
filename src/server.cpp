#include "server.hpp"
#include "packet.hpp"
#include "network.hpp"
#include "log.hpp"
#include "config.hpp"
#include <thread>
#include <chrono>

CoopServer::CoopServer(int p) : port(p) {}

void CoopServer::runLoop() {
    NetworkSystemType type = (gServerConfig.networkSystem == 1) ? SYS_COOPNET : SYS_SOCKET;
    if (!networkInit(type, port)) {
        Logging::log("SERVER", "Failed to initialize network system!");
        return;
    }

    while (true) {
        if (gNetworkSystem) {
            gNetworkSystem->update();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    networkShutdown();
}