#include "app/Config.hpp"

namespace virtualvaultfs::app {

Config loadConfig(const std::filesystem::path& path)
{
    Config config;
    config.databasePath = path;
    return config;
}

} // namespace virtualvaultfs::app
