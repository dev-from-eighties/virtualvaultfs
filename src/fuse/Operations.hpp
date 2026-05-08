#pragma once

#include "storage/ObjectStore.hpp"
#include "vfs/NodeStore.hpp"

#include <optional>
#include <span>
#include <sys/stat.h>
#include <vector>

namespace virtualvaultfs::fuse {

class Operations {
public:
    Operations(vfs::NodeStore& nodes, storage::ObjectStore& objects);

    int getattr(const vfs::Node& node, struct stat* st) const;
    vfs::Node root() const;
    std::optional<vfs::Node> lookup(std::int64_t parentId, const std::string& name) const;
    std::vector<vfs::Node> readdir(std::int64_t directoryId) const;
    vfs::FileHandle open(const vfs::Node& file, int flags) const;
    std::vector<std::byte> read(const vfs::FileHandle& handle, std::size_t size, std::int64_t offset) const;
    std::size_t write(const vfs::FileHandle& handle, std::span<const std::byte> data, std::int64_t offset) const;
    std::optional<vfs::Node> symlink(std::int64_t parentId, const std::string& newName, const std::string& target, std::int64_t uid, std::int64_t gid);
    std::optional<std::string> readlink(const vfs::Node& node) const;
    void truncate(const vfs::Node& node, std::int64_t size, const vfs::FileHandle* handle = nullptr);
    void chmod(const vfs::Node& node, std::int64_t mode);
    void chown(const vfs::Node& node, std::int64_t uid, std::int64_t gid);
    bool access(const vfs::Node& node, int mask) const;
    void rename(std::int64_t nodeId, std::int64_t newParentId, const std::string& newName);
    bool renameReplace(std::int64_t nodeId, std::int64_t newParentId, const std::string& newName);
    bool unlink(std::int64_t nodeId, bool removeObject);
    vfs::Node mkdir(std::int64_t parentId, const std::string& name, std::int64_t mode, std::int64_t uid, std::int64_t gid);

    /** Create a new file (vfs::Node) and register it in DB.
     *
     * @param parentId The ID of the parent node. Never 0. Root is 1.
     *
     * @param newName A string representing the visible name of the file.
     *
     * @return vfs::Node representing the new node of the file in Data Base. node.id = -1 if error.
     */
    std::optional<vfs::Node> create(std::int64_t parentId, const std::string& newName, std::int64_t mode, std::int64_t uid, std::int64_t gid);


    bool updateTimes(std::int64_t nodeId, std::int64_t atime_ns, std::int64_t mtime_ns);

private:
    vfs::NodeStore& nodes_;
    storage::ObjectStore& objects_;
};

} // namespace virtualvaultfs::fuse
