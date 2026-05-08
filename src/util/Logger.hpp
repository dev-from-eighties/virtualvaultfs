#pragma once

#include <string_view>

namespace virtualvaultfs::util {

class Logger {
public:
    static void info(std::string_view message);
    static void error(std::string_view message);
};

} // namespace virtualvaultfs::util
