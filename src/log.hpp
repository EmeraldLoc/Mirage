#pragma once

#include <iostream>
#include <format>
#include <string>

namespace Logging {
    template <typename... Args>
    void log(const std::string &title, std::format_string<Args...> fmt, Args &&...args) {
        std::string str = std::format(fmt, std::forward<Args>(args)...);

        for (size_t pos = 0; (pos = str.find("\\#", pos)) != std::string::npos;) {
            const auto end = str.find('\\', pos + 2);
            if (end == std::string::npos) break;
    
            str.erase(pos, end - pos + 1);
        }

        std::cout << std::format("[{}] {}\n", title, str);
    }
}