#pragma once

#include "vfs/FileHandle.hpp"

#include <filesystem>
#include <string>

namespace virtualvaultfs::storage {

class ObjectStore {
public:
    explicit ObjectStore(std::filesystem::path root);

    std::filesystem::path root() const;
    std::filesystem::path pathFor(const std::string& objectId) const;
    vfs::FileHandle open(const std::string& relpath, int flags, int mode = 0644) const;
    vfs::FileHandle create(const std::string& relpath, int mode = 0644) const;
    bool remove(const std::string& relpath) const;
    
private:
    std::filesystem::path root_;
};

} // namespace virtualvaultfs::storage
