#pragma once

#include <filesystem>

namespace virtualvaultfs::storage {

struct BackendPaths {
    std::filesystem::path root;
    std::filesystem::path objects;
    std::filesystem::path metadata;
};

BackendPaths makeBackendPaths(const std::filesystem::path& root);

} // namespace virtualvaultfs::storage
