#pragma once

#include <iostream>
#include <sstream>
#include <format>
#include <regex>
#include <string>

namespace Logging {
    template <typename... Args>
    void log(const std::string &title, std::format_string<Args...> fmt, Args &&...args) {
        std::string str = std::format(fmt, std::forward<Args>(args)...);

        std::regex pattern(R"(\\#(?:([0-9a-fA-F]{6})|([0-9a-fA-F]{3}))\\|(\\#\\))");

        auto it = std::sregex_iterator(str.begin(), str.end(), pattern);
        auto end = std::sregex_iterator();

        std::string result;
        size_t lastPos = 0;
        bool colorApplied = false;

        for (; it != end; it++) {
            const std::smatch &match = *it;
            result.append(str, lastPos, match.position() - lastPos);

            uint8_t r = 0, g = 0, b = 0;

            if (match[1].matched) {
                std::string hex = match[1].str();
                r = std::stoi(hex.substr(0, 2), nullptr, 16);
                g = std::stoi(hex.substr(2, 2), nullptr, 16);
                b = std::stoi(hex.substr(4, 2), nullptr, 16);
            } else if (match[2].matched) {
                std::string hex = match[2].str();
                r = std::stoi(std::string(2, hex[0]), nullptr, 16);
                g = std::stoi(std::string(2, hex[1]), nullptr, 16);
                b = std::stoi(std::string(2, hex[2]), nullptr, 16);
            } else if (match[3].matched) {
                r = g = b = 0xff;
            }

            std::ostringstream ansiSeq;
            ansiSeq << "\e[38;2;" << static_cast<int>(r) << ";" << static_cast<int>(g) << ";" << static_cast<int>(b) << "m";
            result.append(ansiSeq.str());
            colorApplied = true;

            lastPos = match.position() + match.length();
        }
        result.append(str, lastPos, std::string::npos);

        if (colorApplied) {
            result.append("\033[0m");
        }

        std::cout << std::format("[{}] {}\n", title, result);
    }
}