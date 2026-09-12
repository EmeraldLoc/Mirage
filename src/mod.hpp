#pragma once

#include <cstdint>
#include <vector>
#include <string>

struct CoopModFile {
    std::string relativePath = "";
    std::string realPath = "";
    size_t size = 0;
};

class CoopMod {
public:
    std::string name = "";
    std::vector<CoopModFile> files;
    std::string relativePath = "";
    std::string basePath = "";
    bool isDirectory = false;
    bool pausable = true;
    bool ignoreScriptWarnings = false;
    size_t size = 0;
    
    bool load(const std::string &modPath);
    void extractFields(const std::string &mainFilePath);
};