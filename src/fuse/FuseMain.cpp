#include "fuse/FuseMain.hpp"

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <csignal>
#include <ctime>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define VV_FUSE_FILL_DIR_DEFAULTS static_cast<fuse_fill_dir_flags>(0)

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0U)
#endif

#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1U << 1U)
#endif

#ifndef RENAME_WHITEOUT
#define RENAME_WHITEOUT (1U << 2U)
#endif

#include "app/Config.hpp"
#include "db/Database.hpp"
#include "Operations.hpp"
#include "storage/ObjectStore.hpp"
#include "../util/Logger.hpp"
#include <format>

namespace virtualvaultfs::app {
extern Config* config;
}

namespace virtualvaultfs::fuse {

namespace {

constexpr std::int64_t rootId = 1;

struct ParentAndName {
    std::string parentPath;
    std::string name;
};

struct FuseContext {
    db::Database database;
    vfs::NodeStore nodes;
    storage::ObjectStore objects;
    Operations operations;

    explicit FuseContext(const app::Config& config)
        : database(config.databasePath)
        , nodes(database)
        , objects(config.databasePath.parent_path() / "objects")
        , operations(nodes, objects)
    {
    }
};

Operations& currentOperations()
{
    return *static_cast<Operations*>(fuse_get_context()->private_data);
}

int exceptionToErrno()
{
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return -ENOMEM;
    } catch (...) {
        return -EIO;
    }
}

std::optional<ParentAndName> splitParentAndName(const char* path)
{
    if (path == nullptr || std::strcmp(path, "/") == 0) {
        return std::nullopt;
    }

    const char* nameBegin = std::strrchr(path, '/');
    if (nameBegin == nullptr) {
        return std::nullopt;
    }
    ++nameBegin;

    if (*nameBegin == '\0') {
        return std::nullopt;
    }

    ParentAndName result;
    result.name = nameBegin;
    if (nameBegin == path + 1) {
        result.parentPath = "/";
    } else {
        result.parentPath.assign(path, static_cast<std::size_t>(nameBegin - path - 1));
    }
    return result;
}

std::optional<vfs::Node> resolvePath(Operations& operations, const char* path)
{
    if (std::strcmp(path, "/") == 0) {
        return operations.root();
    }

    std::int64_t parentId = rootId;
    const char* part = path;
    while (*part == '/') {
        ++part;
    }

    for (;;) {
        const char* end = part;
        while (*end != '\0' && *end != '/') {
            ++end;
        }

        if (end != part) {
            auto node = operations.lookup(parentId, std::string(part, static_cast<std::size_t>(end - part)));
            if (!node.has_value()) {
                return std::nullopt;
            }

            if (*end == '\0') {
                return node;
            }

            parentId = node->id;
        }

        part = end;
        while (*part == '/') {
            ++part;
        }

        if (*part == '\0') {
            return std::nullopt;
        }
    }
}

vfs::FileHandle& fileHandle(struct fuse_file_info* fi)
{
    return *reinterpret_cast<vfs::FileHandle*>(fi->fh);
}

} // namespace

static int vv_getattr(const char* path, struct stat* st, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("getattr: {}", path));

    (void)fi;

    std::memset(st, 0, sizeof(struct stat));

    auto& operations = currentOperations();

    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    return operations.getattr(*node, st);
}

static int vv_readdir(
    const char* path,
    void* buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info* fi,
    enum fuse_readdir_flags flags)
{
    util::Logger::info(std::format("readdir: {}", path));

    (void)offset;
    (void)fi;
    (void)flags;

    filler(buf, ".",  nullptr, 0, VV_FUSE_FILL_DIR_DEFAULTS);
    filler(buf, "..", nullptr, 0, VV_FUSE_FILL_DIR_DEFAULTS);

    auto& operations = currentOperations();
    const auto directory = resolvePath(operations, path);
    if (!directory.has_value()) {
        return -ENOENT;
    }

    if (directory->type != vfs::NodeType::Directory) {
        return -ENOTDIR;
    }

    for (const auto& node : operations.readdir(directory->id)) {
        if (filler(buf, node.name.c_str(), nullptr, 0, VV_FUSE_FILL_DIR_DEFAULTS) != 0) {
            break;
        }
    }

    return 0;
}

static int vv_mkdir(const char* path, mode_t mode)
{
    util::Logger::info(std::format("mkdir: {}", path));

    const auto parsed = splitParentAndName(path);
    if (!parsed.has_value()) {
        return -EINVAL;
    }

    auto& operations = currentOperations();

    const auto parent = resolvePath(operations, parsed->parentPath.c_str());
    if (!parent.has_value()) {
        return -ENOENT;
    }

    if (parent->type != vfs::NodeType::Directory) {
        return -ENOTDIR;
    }

    if (operations.lookup(parent->id, parsed->name).has_value()) {
        return -EEXIST;
    }

    try {
        operations.mkdir(parent->id, parsed->name, static_cast<std::int64_t>(mode & 07777), getuid(), getgid());
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_rmdir(const char* path)
{
    util::Logger::info(std::format("rmdir: {}", path));

    if (path == nullptr) {
        return -EINVAL;
    }

    if (std::strcmp(path, "/") == 0) {
        return -EBUSY;
    }

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    if (node->type != vfs::NodeType::Directory) {
        return -ENOTDIR;
    }

    if (!operations.readdir(node->id).empty()) {
        return -ENOTEMPTY;
    }

    if (!operations.unlink(node->id, false)) {
        return -ENOENT;
    }

    return 0;
}

static int vv_open(const char* path, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("open: {}", path));

    if (fi == nullptr) {
        return -EINVAL;
    }

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    if (node->type == vfs::NodeType::Directory) {
        return -EISDIR;
    }

    try {
        auto handle = std::make_unique<vfs::FileHandle>(operations.open(*node, fi->flags));
        fi->fh = reinterpret_cast<std::uint64_t>(handle.release());
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_create(const char* path, mode_t mode, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("create: {}", path));

    if (fi == nullptr) {
        return -EINVAL;
    }

    const auto parsed = splitParentAndName(path);
    if (!parsed.has_value()) {
        return -EINVAL;
    }

    auto& operations = currentOperations();
    const auto parent = resolvePath(operations, parsed->parentPath.c_str());
    if (!parent.has_value()) {
        return -ENOENT;
    }

    if (parent->type != vfs::NodeType::Directory) {
        return -ENOTDIR;
    }

    if (operations.lookup(parent->id, parsed->name).has_value()) {
        return -EEXIST;
    }

    try {
        const auto node = operations.create(
            parent->id,
            parsed->name,
            static_cast<std::int64_t>(mode & 07777),
            getuid(),
            getgid());
        if (!node.has_value()) {
            return -ENOENT;
        }

        try {
            const int openFlags = fi->flags & ~(O_CREAT | O_EXCL);
            auto handle = std::make_unique<vfs::FileHandle>(operations.open(*node, openFlags));
            fi->fh = reinterpret_cast<std::uint64_t>(handle.release());
        } catch (...) {
            operations.unlink(node->id, true);
            throw;
        }
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_symlink(const char* target, const char* path)
{
    util::Logger::info(std::format("symlink: {} -> {}", path, target));

    if (target == nullptr) {
        return -EINVAL;
    }

    const auto parsed = splitParentAndName(path);
    if (!parsed.has_value()) {
        return -EINVAL;
    }

    auto& operations = currentOperations();
    const auto parent = resolvePath(operations, parsed->parentPath.c_str());
    if (!parent.has_value()) {
        return -ENOENT;
    }

    if (parent->type != vfs::NodeType::Directory) {
        return -ENOTDIR;
    }

    if (operations.lookup(parent->id, parsed->name).has_value()) {
        return -EEXIST;
    }

    try {
        const auto node = operations.symlink(parent->id, parsed->name, target, getuid(), getgid());
        if (!node.has_value()) {
            return -ENOENT;
        }
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_readlink(const char* path, char* buf, std::size_t size)
{
    util::Logger::info(std::format("readlink: {}", path));

    if (buf == nullptr || size == 0) {
        return -EINVAL;
    }

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    try {
        const auto target = operations.readlink(*node);
        if (!target.has_value()) {
            return -EINVAL;
        }

        const std::size_t copySize = target->size() < size ? target->size() : size - 1;
        std::memcpy(buf, target->data(), copySize);
        buf[copySize] = '\0';
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_read(const char* path, char* buf, std::size_t size, off_t offset, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("read: {}", path));

    (void)path;

    if (buf == nullptr || fi == nullptr || fi->fh == 0) {
        return -EINVAL;
    }

    try {
        const auto data = currentOperations().read(fileHandle(fi), size, offset);
        std::memcpy(buf, data.data(), data.size());
        return static_cast<int>(data.size());
    } catch (...) {
        return exceptionToErrno();
    }
}

static int vv_write(const char* path, const char* buf, std::size_t size, off_t offset, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("write: {}", path));

    (void)path;

    if (buf == nullptr || fi == nullptr || fi->fh == 0) {
        return -EINVAL;
    }

    try {
        const auto* bytes = reinterpret_cast<const std::byte*>(buf);
        return static_cast<int>(currentOperations().write(fileHandle(fi), std::span<const std::byte>{bytes, size}, offset));
    } catch (...) {
        return exceptionToErrno();
    }
}

static int vv_release(const char* path, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("release: {}", path));

    (void)path;

    if (fi == nullptr || fi->fh == 0) {
        return 0;
    }

    delete reinterpret_cast<vfs::FileHandle*>(fi->fh);
    fi->fh = 0;
    return 0;
}

static int vv_unlink(const char* path)
{
    util::Logger::info(std::format("unlink: {}", path));

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    if (node->type == vfs::NodeType::Directory) {
        return -EISDIR;
    }

    try {
        if (!operations.unlink(node->id, true)) {
            return -ENOENT;
        }
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_rename(const char* from, const char* to, unsigned int flags)
{
    util::Logger::info(std::format("rename: {} -> {}", from, to));

    if ((flags & (RENAME_EXCHANGE | RENAME_WHITEOUT)) != 0) {
        return -EOPNOTSUPP;
    }

    if ((flags & ~RENAME_NOREPLACE) != 0) {
        return -EINVAL;
    }

    if (from == nullptr || to == nullptr || std::strcmp(from, "/") == 0) {
        return -EINVAL;
    }

    const auto parsedTo = splitParentAndName(to);
    if (!parsedTo.has_value()) {
        return -EINVAL;
    }

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, from);
    if (!node.has_value()) {
        return -ENOENT;
    }

    const auto newParent = resolvePath(operations, parsedTo->parentPath.c_str());
    if (!newParent.has_value()) {
        return -ENOENT;
    }

    if (newParent->type != vfs::NodeType::Directory) {
        return -ENOTDIR;
    }

    const auto target = operations.lookup(newParent->id, parsedTo->name);
    if (target.has_value()) {
        if ((flags & RENAME_NOREPLACE) != 0) {
            return -EEXIST;
        }

        if (target->id == node->id) {
            return 0;
        }

        if (node->type == vfs::NodeType::Directory && target->type != vfs::NodeType::Directory) {
            return -ENOTDIR;
        }

        if (node->type != vfs::NodeType::Directory && target->type == vfs::NodeType::Directory) {
            return -EISDIR;
        }

        if (target->type == vfs::NodeType::Directory && !operations.readdir(target->id).empty()) {
            return -ENOTEMPTY;
        }
    }

    try {
        if (target.has_value()) {
            operations.renameReplace(node->id, newParent->id, parsedTo->name);
        } else {
            operations.rename(node->id, newParent->id, parsedTo->name);
        }
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_utimens(const char* path,
                      const struct timespec tv[2],
                      struct fuse_file_info* fi)
{
    (void)fi;

    auto& operations = currentOperations();
    auto node = resolvePath(operations, path);

    if (!node.has_value()) {
        return -ENOENT;
    }

    auto now_ns = [] {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return ts.tv_sec * 1000000000LL + ts.tv_nsec;
    };

    int64_t atime_ns = node->mtimeNs;
    int64_t mtime_ns = node->mtimeNs;

    if (node->type == vfs::NodeType::File)
    {
        struct stat st{};
        operations.getattr(*node, &st);
    }

    // atime
    if (tv[0].tv_nsec == UTIME_NOW) {
        atime_ns = now_ns();
    } else if (tv[0].tv_nsec == UTIME_OMIT) {
        // don't touch
    } else {
        atime_ns = tv[0].tv_sec * 1000000000LL + tv[0].tv_nsec;
    }

    // mtime
    if (tv[1].tv_nsec == UTIME_NOW) {
        mtime_ns = now_ns();
    } else if (tv[1].tv_nsec == UTIME_OMIT) {
        // no tocar
    } else {
        mtime_ns = tv[1].tv_sec * 1000000000LL + tv[1].tv_nsec;
    }

    if (!operations.updateTimes(node->id, atime_ns, mtime_ns))
    {
        return -EPERM;
    }

    return 0;
}

static int vv_truncate(const char* path, off_t size, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("truncate: {} {}", path, size));

    if (size < 0) {
        return -EINVAL;
    }

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    if (node->type == vfs::NodeType::Directory) {
        return -EISDIR;
    }

    try {
        const vfs::FileHandle* handle = (fi != nullptr && fi->fh != 0) ? &fileHandle(fi) : nullptr;
        operations.truncate(*node, static_cast<std::int64_t>(size), handle);
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_fsync(const char* path, int datasync, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("fsync: {}", path));

    (void)path;

    if (fi == nullptr || fi->fh == 0) {
        return -EINVAL;
    }

    try {
        fileHandle(fi).sync(datasync != 0);
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_flush(const char* path, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("flush: {}", path));

    (void)fi;
    return 0;
}

static int vv_access(const char* path, int mask)
{
    util::Logger::info(std::format("access: {} {}", path, mask));

    if ((mask & ~(R_OK | W_OK | X_OK | F_OK)) != 0) {
        return -EINVAL;
    }

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    return operations.access(*node, mask) ? 0 : -EACCES;
}

static int vv_chmod(const char* path, mode_t mode, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("chmod: {} {:o}", path, mode));

    (void)fi;

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    try {
        operations.chmod(*node, static_cast<std::int64_t>(mode));
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi)
{
    util::Logger::info(std::format("chown: {} {} {}", path, uid, gid));

    (void)fi;

    auto& operations = currentOperations();
    const auto node = resolvePath(operations, path);
    if (!node.has_value()) {
        return -ENOENT;
    }

    try {
        operations.chown(*node, static_cast<std::int64_t>(uid), static_cast<std::int64_t>(gid));
    } catch (...) {
        return exceptionToErrno();
    }

    return 0;
}

static int vv_statfs (const char* fs, struct statvfs* out)
{
    (void)fs;
    if (out == nullptr || app::config == nullptr) {
        return -EINVAL;
    }

    const auto storagePath = app::config->databasePath.parent_path();
    if (::statvfs(storagePath.c_str(), out) != 0) {
        return -errno;
    }

    return 0;
}

int runFuse(int argc, char** argv)
{
    static fuse_operations operations{};
    if (app::config == nullptr) {
        return -EINVAL;
    }

    operations.getattr = vv_getattr;
    operations.readdir = vv_readdir;
    operations.mkdir = vv_mkdir;
    operations.rmdir = vv_rmdir;
    operations.open = vv_open;
    operations.create = vv_create;
    operations.symlink = vv_symlink;
    operations.readlink = vv_readlink;
    operations.read = vv_read;
    operations.write = vv_write;
    operations.release = vv_release;
    operations.unlink = vv_unlink;
    operations.rename = vv_rename;
    operations.utimens = vv_utimens;
    operations.statfs = vv_statfs;
    operations.truncate = vv_truncate;
    operations.fsync = vv_fsync;
    operations.flush = vv_flush;
    operations.access = vv_access;
    operations.chmod = vv_chmod;
    operations.chown = vv_chown;

    auto context = std::make_unique<FuseContext>(*app::config);
    return fuse_main(argc, argv, &operations, &context->operations);
}

} // namespace virtualvaultfs::fuse
