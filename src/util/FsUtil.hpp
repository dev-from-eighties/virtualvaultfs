#pragma once

#include <filesystem>

namespace virtualvaultfs::util {

bool ensureDirectory(const std::filesystem::path& path);

} // namespace virtualvaultfs::util
