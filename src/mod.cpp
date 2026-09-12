#include <fstream>
#include <iostream>
#include <filesystem>
#include "mod.hpp"
#include "config.hpp"

bool CoopMod::extractFields(CoopMod &mod, const std::string &modPath) {
    size_t lastDot = modPath.rfind('.');
    size_t lastSlash = modPath.rfind('/');

    if (lastDot == std::string::npos || (lastSlash != std::string::npos && lastDot < lastSlash)) {
        mod.isDirectory = true;
    }

    std::string mainPath = modPath;

    if (mod.isDirectory) {
        mainPath += "/main.lua";
        if (!(std::filesystem::exists(mainPath) && std::filesystem::is_regular_file(mainPath))) {
            return false;
        }
    }

    CoopModFile modFile;
    modFile.relativePath = mainPath;
    
    std::ifstream file(mainPath, std::ios::binary);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            // if line doesn't start with `--` then end the search
            if (line.rfind("-- ", 0)) {
                std::cout << "Stopping" << std::endl;
                std::cout << "Line:\n" << line << std::endl;
                break;
            }
            std::cout << "Continuing at Line:\n" << line << std::endl;

            line = line.substr(3);

            size_t delimPos = line.find(": ");
            if (delimPos != std::string::npos) {
                std::string key   = line.substr(0, delimPos);
                std::string value = line.substr(delimPos + 2);
                std::cout << key << ": " << value << std::endl;
                if (key == "name") {
                    mod.name = value;
                } else if (key == "pausable") {
                    mod.pausable = value == "true";
                } else if (key == "ignore-script-warnings") {
                    mod.ignoreScriptWarnings = value == "true";
                }
            }
        }
        file.seekg(0, std::ios::end);
        modFile.size = file.tellg();
        file.close();
    }
    mod.size = modFile.size;
    return true;
}

bool CoopMod::load(CoopMod &mod) {
    gServerConfig.mods.push_back(mod);
    return true;
}