#include "fuse/Operations.hpp"

#include "util/Error.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <string>
#include <sys/stat.h>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>

namespace virtualvaultfs::fuse {

namespace {

mode_t typeMode(vfs::NodeType type)
{
    switch (type) {
    case vfs::NodeType::Directory:
        return S_IFDIR;
    case vfs::NodeType::Symlink:
        return S_IFLNK;
    case vfs::NodeType::File:
        return S_IFREG;
    }
    return S_IFREG;
}

void setTimespec(timespec& target, std::int64_t ns)
{
    target.tv_sec = static_cast<time_t>(ns / 1'000'000'000LL);
    target.tv_nsec = static_cast<long>(ns % 1'000'000'000LL);
}

} // namespace

Operations::Operations(vfs::NodeStore& nodes, storage::ObjectStore& objects)
    : nodes_(nodes)
    , objects_(objects)
{
}

int Operations::getattr(const vfs::Node& node, struct stat* st) const
{
    if (st == nullptr) {
        return -EINVAL;
    }

    std::memset(st, 0, sizeof(*st));
    st->st_ino = static_cast<ino_t>(node.id);
    st->st_mode = typeMode(node.type) | static_cast<mode_t>(node.mode);
    st->st_nlink = node.type == vfs::NodeType::Directory ? 2U : 1U;
    st->st_uid = static_cast<uid_t>(node.uid);
    st->st_gid = static_cast<gid_t>(node.gid);
    st->st_size = static_cast<off_t>(node.size);
    setTimespec(st->st_atim, node.mtimeNs);
    setTimespec(st->st_mtim, node.mtimeNs);
    setTimespec(st->st_ctim, node.ctimeNs);

    if (node.type != vfs::NodeType::File || node.objectRelpath.empty()) {
        return 0;
    }

    struct stat physical {};
    if (::stat(objects_.pathFor(node.objectRelpath).c_str(), &physical) != 0) {
        return -errno;
    }

    st->st_mode = typeMode(node.type) | (physical.st_mode & 07777);
    st->st_uid = physical.st_uid;
    st->st_gid = physical.st_gid;
    st->st_size = physical.st_size;
    st->st_atim = physical.st_atim;
    st->st_mtim = physical.st_mtim;
    st->st_ctim = physical.st_ctim;
    return 0;
}

vfs::Node Operations::root() const
{
    return nodes_.root();
}

std::optional<vfs::Node> Operations::lookup(std::int64_t parentId, const std::string& name) const
{
    return nodes_.lookup(parentId, name);
}

std::vector<vfs::Node> Operations::readdir(std::int64_t directoryId) const
{
    return nodes_.readdir(directoryId);
}

vfs::FileHandle Operations::open(const vfs::Node& file, int flags) const
{
    if (file.type != vfs::NodeType::File || file.objectRelpath.empty()) {
        throw util::Error("node is not an object-backed file");
    }

    return objects_.open(file.objectRelpath, flags);
}

std::vector<std::byte> Operations::read(const vfs::FileHandle& handle, std::size_t size, std::int64_t offset) const
{
    return handle.read(size, offset);
}

std::size_t Operations::write(const vfs::FileHandle& handle, std::span<const std::byte> data, std::int64_t offset) const
{
    return handle.write(data, offset);
}

std::optional<vfs::Node> Operations::symlink(std::int64_t parentId, const std::string& newName, const std::string& target, std::int64_t uid, std::int64_t gid)
{
    const auto parent = nodes_.find(parentId);
    if (!parent.has_value() || parent->type != vfs::NodeType::Directory) {
        return std::nullopt;
    }

    auto now = std::chrono::system_clock::now();
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    const std::int64_t object_id = nodes_.createObject();
    std::string relpath;

    try {
        relpath = std::format("{}.blob", object_id);
        if (::symlink(target.c_str(), objects_.pathFor(relpath).c_str()) != 0) {
            throw util::Error(std::string{"symlink failed: "} + std::strerror(errno));
        }

        nodes_.updateObject(object_id, relpath, target.size());

        vfs::Node node;
        node.uid = uid;
        node.gid = gid;
        node.mode = 0777;
        node.name = newName;
        node.ctimeNs = now_ns;
        node.mtimeNs = now_ns;
        node.size = static_cast<std::int64_t>(target.size());
        node.type = vfs::NodeType::Symlink;
        node.parentId = parentId;
        node.objectId = object_id;
        node.objectRelpath = relpath;

        return nodes_.add(node);
    } catch (...) {
        if (!relpath.empty()) {
            objects_.remove(relpath);
        }
        nodes_.removeObject(object_id);
        throw;
    }
}

std::optional<std::string> Operations::readlink(const vfs::Node& node) const
{
    if (node.type != vfs::NodeType::Symlink || node.objectRelpath.empty()) {
        return std::nullopt;
    }

    const auto path = objects_.pathFor(node.objectRelpath);
    std::string target(static_cast<std::size_t>(node.size > 0 ? node.size : 4096), '\0');
    for (;;) {
        const auto length = ::readlink(path.c_str(), target.data(), target.size());
        if (length < 0) {
            throw util::Error(std::string{"readlink failed: "} + std::strerror(errno));
        }

        if (static_cast<std::size_t>(length) < target.size()) {
            target.resize(static_cast<std::size_t>(length));
            return target;
        }

        target.resize(target.size() * 2);
    }
}

void Operations::truncate(const vfs::Node& node, std::int64_t size, const vfs::FileHandle* handle)
{
    if (node.type != vfs::NodeType::File || !node.objectId.has_value() || node.objectRelpath.empty()) {
        throw util::Error("node is not an object-backed file");
    }

    if (handle != nullptr) {
        handle->truncate(size);
    } else {
        auto opened = objects_.open(node.objectRelpath, O_WRONLY);
        opened.truncate(size);
    }

    auto updated = node;
    updated.size = size;
    nodes_.updateNode(updated);
    nodes_.updateObject(*node.objectId, node.objectRelpath, static_cast<std::size_t>(size));
}

void Operations::chmod(const vfs::Node& node, std::int64_t mode)
{
    auto updated = node;
    updated.mode = mode & 07777;

    if (node.type == vfs::NodeType::File && !node.objectRelpath.empty()) {
        if (::chmod(objects_.pathFor(node.objectRelpath).c_str(), static_cast<mode_t>(updated.mode)) != 0) {
            throw util::Error(std::string{"chmod failed: "} + std::strerror(errno));
        }
    }

    nodes_.updateNode(updated);
}

void Operations::chown(const vfs::Node& node, std::int64_t uid, std::int64_t gid)
{
    auto updated = node;
    if (uid != -1) {
        updated.uid = uid;
    }
    if (gid != -1) {
        updated.gid = gid;
    }

    if (node.type == vfs::NodeType::File && !node.objectRelpath.empty()) {
        if (::chown(objects_.pathFor(node.objectRelpath).c_str(), static_cast<uid_t>(uid), static_cast<gid_t>(gid)) != 0) {
            throw util::Error(std::string{"chown failed: "} + std::strerror(errno));
        }
    }

    nodes_.updateNode(updated);
}

bool Operations::access(const vfs::Node& node, int mask) const
{
    if (node.type == vfs::NodeType::File && !node.objectRelpath.empty()) {
        return ::access(objects_.pathFor(node.objectRelpath).c_str(), mask) == 0;
    }

    if (mask == F_OK || geteuid() == 0) {
        return true;
    }

    int shift = 0;
    if (static_cast<std::int64_t>(geteuid()) == node.uid) {
        shift = 6;
    } else if (static_cast<std::int64_t>(getegid()) == node.gid) {
        shift = 3;
    }

    int required = 0;
    if ((mask & R_OK) != 0) {
        required |= 4;
    }
    if ((mask & W_OK) != 0) {
        required |= 2;
    }
    if ((mask & X_OK) != 0) {
        required |= 1;
    }

    return ((node.mode >> shift) & required) == required;
}

void Operations::rename(std::int64_t nodeId, std::int64_t newParentId, const std::string& newName)
{
    nodes_.rename(nodeId, newParentId, newName);
}

bool Operations::renameReplace(std::int64_t nodeId, std::int64_t newParentId, const std::string& newName)
{
    const auto replaced = nodes_.renameReplace(nodeId, newParentId, newName);
    if (replaced.has_value() && !replaced->objectRelpath.empty()) {
        objects_.remove(replaced->objectRelpath);
    }
    return true;
}

bool Operations::unlink(std::int64_t nodeId, bool removeObject)
{
    const auto node = nodes_.find(nodeId);
    if (!node.has_value()) {
        return false;
    }

    const auto objectId = nodes_.unlink(nodeId);
    if (removeObject && objectId.has_value() && !node->objectRelpath.empty()) {
        objects_.remove(node->objectRelpath);
        nodes_.removeObject(*objectId);
    }
    return true;
}

vfs::Node Operations::mkdir(std::int64_t parentId, const std::string& name, std::int64_t mode, std::int64_t uid, std::int64_t gid)
{
    return nodes_.mkdir(parentId, name, mode, uid, gid);
}

std::optional<vfs::Node> Operations::create(std::int64_t parentId, const std::string& newName, std::int64_t mode, std::int64_t uid, std::int64_t gid)
{
    // check if parentId really returns a node
    const auto parent = nodes_.find(parentId);
    if (!parent.has_value()) {
        return std::nullopt;
    }

    if (parent->type != vfs::NodeType::Directory) {
        return std::nullopt;
    }

    // Remember time now
    auto now = std::chrono::system_clock::now();
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    // Reserve object id in database
    int64_t object_id = nodes_.createObject();
    std::string relpath;

    try {
        // Try to create a new blob object in filesystem.
        relpath = std::format("{}.blob", object_id);
        auto fh = objects_.create(relpath, static_cast<int>(mode));

        // object blob exists in filesystem at this time, update relpath in objects table on database
        nodes_.updateObject(object_id, relpath, 0);

        // add node to store (causing insert of new row in database)
        vfs::Node node;
        node.uid = uid;
        node.gid = gid;
        node.mode = mode;
        node.name = newName;
        node.ctimeNs = now_ns;
        node.mtimeNs = now_ns;
        node.size = 0;
        node.type = vfs::NodeType::File;
        node.parentId = parentId;
        node.objectId = object_id;
        node.objectRelpath = relpath;

        return nodes_.add(node);
    } catch (...) {
        if (!relpath.empty()) {
            objects_.remove(relpath);
        }
        nodes_.removeObject(object_id);
        throw;
    }
}

bool Operations::updateTimes(std::int64_t nodeId, std::int64_t atime_ns, std::int64_t mtime_ns)
{
    auto node = nodes_.find(nodeId);

    if (!node.has_value())
    {
        return false;
    }

    if (node->type == vfs::NodeType::File)
    {
        struct timespec ts[2];

        ts[0].tv_sec  = atime_ns / 1'000'000'000LL;
        ts[0].tv_nsec = atime_ns % 1'000'000'000LL;

        ts[1].tv_sec  = mtime_ns / 1'000'000'000LL;
        ts[1].tv_nsec = mtime_ns % 1'000'000'000LL;

        std::filesystem::path blob_path = objects_.pathFor(std::format("{}.blob", *(node->objectId)));
        if (::utimensat(AT_FDCWD, blob_path.c_str(), ts, 0) != 0) {
            return false;
        }
    } else
    {
        node->mtimeNs = mtime_ns;
        // We ignore atime for now

        nodes_.updateNode(*node);
    }

    return true;
}

} // namespace virtualvaultfs::fuse
