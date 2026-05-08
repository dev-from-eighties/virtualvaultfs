#include "util/Logger.hpp"

#include <iostream>

namespace virtualvaultfs::util {

void Logger::info(std::string_view message)
{
    std::cout << message << '\n';
}

void Logger::error(std::string_view message)
{
    std::cerr << message << '\n';
}

} // namespace virtualvaultfs::util
