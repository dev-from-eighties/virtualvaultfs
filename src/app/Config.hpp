#pragma once

#include <filesystem>
#include <string>

namespace virtualvaultfs::app {

struct Config {
    std::string vaultName{"default"};
    std::filesystem::path mountPoint;
    std::filesystem::path storageRoot{"vault-data"};
    std::filesystem::path databasePath{"vault-data/metadata.sqlite3"};
};

Config loadConfig(const std::filesystem::path& path);

} // namespace virtualvaultfs::app
