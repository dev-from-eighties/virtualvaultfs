#include "import/Importer.hpp"

namespace virtualvaultfs::importer {

bool Importer::importPath(const std::filesystem::path& path)
{
    return !path.empty();
}

} // namespace virtualvaultfs::importer
