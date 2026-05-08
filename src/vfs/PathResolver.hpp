#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace virtualvaultfs::vfs {

class PathResolver {
public:
    static std::vector<std::string> split(const std::filesystem::path& path);
};

} // namespace virtualvaultfs::vfs
