#pragma once

#include <cstdint>
#include <vector>
#include <string>

struct CoopModFile {
    std::string relativePath = "";
    size_t size = 0;
    bool isLoadedLuaModule = false;

    FILE* fp;
    uint8_t dataHash[16];
    std::string cachedPath = "";
};

class CoopMod {
public:
    std::string name = "";
    std::vector<CoopModFile> files;
    std::string relativePath = "";
    bool isDirectory = false;
    bool pausable = true;
    bool ignoreScriptWarnings = false;
    size_t size = 0;
    static bool extractFields(CoopMod &mod, const std::string &modPath);
    static bool load(CoopMod &mod);
};
