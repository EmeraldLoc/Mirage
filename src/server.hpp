#pragma once

class CoopServer {
private:
    int port;
public:
    CoopServer(int p);
    void runLoop();
    int getPort() const { return port; }
};