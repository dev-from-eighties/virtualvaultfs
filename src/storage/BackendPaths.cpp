#include "storage/BackendPaths.hpp"

namespace virtualvaultfs::storage {

BackendPaths makeBackendPaths(const std::filesystem::path& root)
{
    return BackendPaths{
        .root = root,
        .objects = root / "objects",
        .metadata = root / "metadata.sqlite3",
    };
}

} // namespace virtualvaultfs::storage
