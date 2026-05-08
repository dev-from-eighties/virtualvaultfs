#include "util/FsUtil.hpp"

namespace virtualvaultfs::util {

bool ensureDirectory(const std::filesystem::path& path)
{
    if (std::filesystem::exists(path)) {
        return std::filesystem::is_directory(path);
    }

    return std::filesystem::create_directories(path);
}

} // namespace virtualvaultfs::util
