#pragma once

#include <atomic>

class CoopServer {
private:
    int port;
public:
    CoopServer(int p);
    void runLoop();
    int getPort() const { return port; }
};

extern std::atomic<bool> gServerRunning;