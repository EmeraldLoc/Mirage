#pragma once

#include "network.hpp"
#include "mod.hpp"
#include "json.hpp"

#include <fstream>
#include <string>
#include <vector>

#define SERVER_CONFIGFILE "config.json"

using json = nlohmann::json;

class ServerConfig {
public:
    int port = 7777;
    std::string version = "v1.5.1";
    std::string name = "PeakServer";
    int savefileIndex = 1;
    int playerInteractions = 1;
    int bouncyBounds = 0;
    int knockStrength = 25;
    int starStaying = 1;
    int skipIntro = 1;
    int bubbleDeath = 1;
    int headless = 0;
    int nametags = 1;
    int maxPlayers = MAX_PLAYERS;
    int pauseAnywhere = 1;
    std::vector<CoopMod> mods;
    // int pvpType = 0;

    void read(const std::string &filename);
    void write(const std::string &filename);
};

extern ServerConfig gServerConfig;
