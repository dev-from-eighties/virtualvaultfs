#pragma once

#include <filesystem>

namespace virtualvaultfs::importer {

class Importer {
public:
    bool importPath(const std::filesystem::path& path);
};

} // namespace virtualvaultfs::importer
