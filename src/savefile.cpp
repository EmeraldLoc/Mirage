#include "savefile.hpp"
#include <fstream>
#include <vector>
#include <cstring>

CoopSaveFile gSaveFile;

static uint16_t calculate_checksum(const uint8_t *data, size_t size) {
    uint16_t chksum = 0;
    for (size_t i = 0; i < size - 2; ++i) {
        chksum += data[i];
    }
    return chksum;
}

std::vector<uint8_t> CoopSaveFile::getBuffer() {
    std::vector<uint8_t> buffer(EEPROM_SIZE, 0);
    std::ifstream file(SAVE_FILENAME, std::ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(buffer.data()), EEPROM_SIZE);
        file.close();
    }
    return buffer;
}

void CoopSaveFile::setIndex(int idx) {
    index = idx;
}

void CoopSaveFile::load() {
    std::ifstream file(SAVE_FILENAME, std::ios::binary);
    if (!file.is_open()) return;

    std::vector<uint8_t> buffer(EEPROM_SIZE, 0);
    file.read(reinterpret_cast<char*>(buffer.data()), EEPROM_SIZE);
    file.close();

    size_t fileOffset = this->index * 2 * SAVE_FILE_SIZE;

    std::memcpy(&this->saveFlags, &buffer[fileOffset + 8], sizeof(uint32_t));
    std::memcpy(this->courseStars, &buffer[fileOffset + 12], 25);
}

void CoopSaveFile::save() {
    std::vector<uint8_t> buffer(EEPROM_SIZE, 0);

    std::ifstream inFile(SAVE_FILENAME, std::ios::binary);
    if (inFile.is_open()) {
        inFile.read(reinterpret_cast<char*>(buffer.data()), EEPROM_SIZE);
        inFile.close();
    }

    size_t fileOffsets[2] = {
        this->index * 2 * SAVE_FILE_SIZE,
        (this->index * 2 + 1) * SAVE_FILE_SIZE
    };

    uint16_t magic = 0x4441;
    for (size_t offset : fileOffsets) {
        std::memcpy(&buffer[offset + 8], &this->saveFlags, sizeof(uint32_t));
        std::memcpy(&buffer[offset + 12], this->courseStars, 25);

        std::memcpy(&buffer[offset + 52], &magic, sizeof(uint16_t));

        uint16_t chksum = calculate_checksum(&buffer[offset], SAVE_FILE_SIZE);
        std::memcpy(&buffer[offset + 54], &chksum, sizeof(uint16_t));
    }

    std::ofstream outFile(SAVE_FILENAME, std::ios::binary);
    if (outFile.is_open()) {
        outFile.write(reinterpret_cast<const char*>(buffer.data()), EEPROM_SIZE);
        outFile.close();
    }
}

void CoopSaveFile::setFlags(uint32_t flags, int32_t course, uint8_t courseData) {
    if (course != -1) {
        courseStars[course] |= courseData;
    }
    saveFlags |= flags;
}

void CoopSaveFile::removeFlags(uint32_t flags, int32_t course, uint8_t courseData) {
    if (course == -1) {
        saveFlags ^= flags;
    } else {
        courseStars[course] ^= courseData;
    }
}