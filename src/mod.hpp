#pragma once

#include <string>

class CoopMod {
public:
    std::string name = "";
    std::string luaPath = "";
    size_t size = 0;
    static bool loadLua(const std::string &modPath);
};
