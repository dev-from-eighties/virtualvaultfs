#include "storage/ObjectStore.hpp"

#include "util/Error.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace virtualvaultfs::storage {

ObjectStore::ObjectStore(std::filesystem::path root)
    : root_(std::move(root))
{
}

std::filesystem::path ObjectStore::root() const
{
    return root_;
}

std::filesystem::path ObjectStore::pathFor(const std::string& objectId) const
{
    return root_ / objectId;
}

vfs::FileHandle ObjectStore::open(const std::string& relpath, int flags, int mode) const
{
    const auto path = pathFor(relpath);
    if ((flags & O_CREAT) != 0) {
        std::filesystem::create_directories(path.parent_path());
    }

    const int fd = ::open(path.c_str(), flags, mode);
    if (fd < 0) {
        throw util::Error("failed to open object: " + path.string());
    }

    return vfs::FileHandle{fd};
}

vfs::FileHandle ObjectStore::create(const std::string& relpath, int mode) const
{
    return open(relpath, O_CREAT | O_EXCL | O_RDWR, mode);
}

bool ObjectStore::remove(const std::string& relpath) const
{
    return std::filesystem::remove(pathFor(relpath));
}

} // namespace virtualvaultfs::storage
