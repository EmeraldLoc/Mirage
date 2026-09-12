#include "config.hpp"
#include <iostream>

ServerConfig gServerConfig{};

void ServerConfig::read(const std::string &filename) {
    std::ifstream file(filename);

    if (!file.is_open()) {
        write(filename);
        return;
    }

    json data;
    file >> data;

    port = data.value("port", port);
    version = data.value("version", version);
    name = data.value("name", name);
    savefileIndex = data.value("savefileIndex", savefileIndex);
    playerInteractions = data.value("playerInteractions", playerInteractions);
    bouncyBounds = data.value("bouncyBounds", bouncyBounds);
    knockStrength = data.value("knockStrength", knockStrength);
    starStaying = data.value("starStaying", starStaying);
    skipIntro = data.value("skipIntro", skipIntro);
    bubbleDeath = data.value("bubbleDeath", bubbleDeath);
    headless = data.value("headless", headless);
    nametags = data.value("nametags", nametags);
    maxPlayers = data.value("maxPlayers", maxPlayers);
    pauseAnywhere = data.value("pauseAnywhere", pauseAnywhere);
    
    mods.clear();
    if (data.contains("mods") && data["mods"].is_array()) {
        for (auto &modPathObj : data["mods"]) {
            std::string modPath = modPathObj.get<std::string>();
            CoopMod mod;

            if (CoopMod::extractFields(mod, modPath)) {
                CoopMod::load(mod);
                std::cout << "Loaded mod " << mod.name << '\n';
            } else {
                std::cout << "Failed to open mod from file/path: " << modPath << '\n';
            }
        }
    }
}

void ServerConfig::write(const std::string &filename) {
    json data = {
        {"port", port},
        {"version", version},
        {"name", name},
        {"savefileIndex", savefileIndex},
        {"playerInteractions", playerInteractions},
        {"bouncyBounds", bouncyBounds},
        {"knockStrength", knockStrength},
        {"starStaying", starStaying},
        {"skipIntro", skipIntro},
        {"bubbleDeath", bubbleDeath},
        {"headless", headless},
        {"nametags", nametags},
        {"maxPlayers", maxPlayers},
        {"pauseAnywhere", pauseAnywhere}
    };

    std::ofstream file(filename);
    file << data.dump(4) << '\n';
}
