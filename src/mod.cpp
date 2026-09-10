#include <fstream>
#include <iostream>
#include "mod.hpp"
#include "config.hpp"

bool CoopMod::loadLua(const std::string &modPath) {
    CoopMod newMod;
            
    size_t lastSlash = modPath.find_last_of("/\\");
    newMod.name = (lastSlash != std::string::npos) ? modPath.substr(lastSlash + 1) : modPath;
    newMod.luaPath = modPath;

    std::ifstream modLuaFile(modPath, std::ios::binary);
    if (modLuaFile.is_open()) {
        modLuaFile.seekg(0, std::ios::end);
        newMod.size = modLuaFile.tellg();
        modLuaFile.close();
        gServerConfig.mods.push_back(newMod);
        return true;
    } else {
        return false;
    }
}