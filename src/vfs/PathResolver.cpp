#include "vfs/PathResolver.hpp"

namespace virtualvaultfs::vfs {

std::vector<std::string> PathResolver::split(const std::filesystem::path& path)
{
    std::vector<std::string> parts;
    for (const auto& part : path) {
        const auto value = part.string();
        if (value.empty() || value == "/") {
            continue;
        }
        parts.push_back(value);
    }
    return parts;
}

} // namespace virtualvaultfs::vfs
