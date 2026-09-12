#include <fstream>
#include <filesystem>
#include <algorithm>
#include "mod.hpp"

namespace fs = std::filesystem;

static void loadModFiles(CoopMod &mod, const fs::path &basePath, const std::string &subDir, const std::vector<std::string> &exts, bool recursive) {
    fs::path searchPath = basePath / subDir;
    if (!fs::exists(searchPath) || !fs::is_directory(searchPath)) return;

    auto processFile = [&](const fs::path &path) {
        if (!fs::is_regular_file(path)) return;
        std::string ext = path.extension().string();
        if (std::find(exts.begin(), exts.end(), ext) == exts.end()) return;

        CoopModFile modFile;
        modFile.realPath = path.string();
        
        modFile.relativePath = fs::relative(path, basePath).string();
        std::replace(modFile.relativePath.begin(), modFile.relativePath.end(), '\\', '/');
        
        modFile.size = fs::file_size(path);
        mod.files.push_back(modFile);
        mod.size += modFile.size;
    };

    if (recursive) {
        for (const auto &entry : fs::recursive_directory_iterator(searchPath)) {
            processFile(entry.path());
        }
    } else {
        for (const auto &entry : fs::directory_iterator(searchPath)) {
            processFile(entry.path());
        }
    }
}

void CoopMod::extractFields(const std::string &mainFilePath) {
    std::ifstream file(mainFilePath, std::ios::binary);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("-- ", 0)) break;
        
        line = line.substr(3);
        size_t delimPos = line.find(": ");
        if (delimPos != std::string::npos) {
            std::string key   = line.substr(0, delimPos);
            std::string value = line.substr(delimPos + 2);
            if (!value.empty() && value.back() == '\r') value.pop_back(); 

            if (key == "name") name = value;
            else if (key == "pausable") pausable = (value == "true");
            else if (key == "ignore-script-warnings") ignoreScriptWarnings = (value == "true");
        }
    }
}

bool CoopMod::load(const std::string &modPath) {
    size_t lastDot = modPath.rfind('.');
    size_t lastSlash = modPath.rfind('/');

    if (lastDot == std::string::npos || (lastSlash != std::string::npos && lastDot < lastSlash)) {
        isDirectory = true;
    }

    std::string mainPath = modPath;
    basePath = modPath;
    size = 0;
    files.clear();

    if (isDirectory) {
        mainPath += "/main.lua";
        if (!(fs::exists(mainPath) && fs::is_regular_file(mainPath))) {
            return false;
        }

        fs::path base(modPath);
        loadModFiles(*this, base, "", {".lua", ".luac"}, true);
        loadModFiles(*this, base, "actors", {".bin", ".col"}, false);
        loadModFiles(*this, base, "data", {".bhv"}, false);
        loadModFiles(*this, base, "textures", {".tex"}, true);
        loadModFiles(*this, base, "levels", {".lvl"}, false);
        loadModFiles(*this, base, "sound", {".m64", ".mp3", ".aiff", ".ogg"}, true);
    } else {
        CoopModFile modFile;
        modFile.realPath = mainPath;
        modFile.relativePath = fs::path(mainPath).filename().string();
        modFile.size = fs::file_size(mainPath);
        files.push_back(modFile);
        size = modFile.size;
    }
    
    extractFields(mainPath);
    
    std::sort(files.begin(), files.end(), [](const CoopModFile &a, const CoopModFile &b) {
        return a.relativePath < b.relativePath;
    });

    return true;
}