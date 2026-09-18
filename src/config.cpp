#include "config.hpp"
#include "json.hpp"
#include "log.hpp"
#include <iostream>

using json = nlohmann::json;

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
    networkSystem = data.value("networkSystem", networkSystem);
    version = data.value("version", version);
    name = data.value("name", name);
    game = data.value("game", game);
    mode = data.value("mode", mode);
    description = data.value("description", description);
    password = data.value("password", password);
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

            if (mod.load(modPath)) {
                mods.push_back(mod);
                Logging::log("CONFIG", "Loaded mod {}", mod.name);
            } else {
                Logging::log("CONFIG", "Failed to open mod {}", mod.name);
            }
        }
    }
}

void ServerConfig::write(const std::string &filename) {
    json data = {
        {"port", port},
        {"networkSystem", networkSystem},
        {"version", version},
        {"game", game},
        {"name", name},
        {"mode", mode},
        {"description", description},
        {"password", password},
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
