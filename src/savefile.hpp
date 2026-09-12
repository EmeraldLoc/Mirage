#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

constexpr size_t EEPROM_SIZE = 0x200;
constexpr size_t SAVE_FILE_SIZE = 56;
const std::string SAVE_FILENAME = "sm64_save.bin";

class CoopSaveFile {
private:
    int index = 0;
    uint32_t saveFlags = 0;
    uint8_t courseStars[25];
public:
    std::vector<uint8_t> getBuffer();
    void setIndex(int idx);
    void save();
    void load();
    void setFlags(uint32_t flags, int32_t course, uint8_t courseData);
    void removeFlags(uint32_t flags, int32_t course, uint8_t courseData);
};

extern CoopSaveFile gSaveFile;