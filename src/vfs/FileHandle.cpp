#include "vfs/FileHandle.hpp"

#include "util/Error.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <utility>

namespace virtualvaultfs::vfs {

FileHandle::FileHandle(int fd)
    : fd_(fd)
{
    if (fd_ < 0) {
        throw util::Error("invalid file descriptor");
    }
}

FileHandle::~FileHandle()
{
    if (fd_ >= 0) {
        close(fd_);
    }
}

FileHandle::FileHandle(FileHandle&& other) noexcept
    : fd_(std::exchange(other.fd_, -1))
{
}

FileHandle& FileHandle::operator=(FileHandle&& other) noexcept
{
    if (this != &other) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

int FileHandle::fd() const
{
    return fd_;
}

std::vector<std::byte> FileHandle::read(std::size_t size, std::int64_t offset) const
{
    std::vector<std::byte> buffer(size);
    const auto bytesRead = pread(fd_, buffer.data(), buffer.size(), offset);
    if (bytesRead < 0) {
        throw util::Error(std::string{"read failed: "} + std::strerror(errno));
    }

    buffer.resize(static_cast<std::size_t>(bytesRead));
    return buffer;
}

std::size_t FileHandle::write(std::span<const std::byte> data, std::int64_t offset) const
{
    const auto bytesWritten = pwrite(fd_, data.data(), data.size(), offset);
    if (bytesWritten < 0) {
        throw util::Error(std::string{"write failed: "} + std::strerror(errno));
    }

    return static_cast<std::size_t>(bytesWritten);
}

void FileHandle::truncate(std::int64_t size) const
{
    if (ftruncate(fd_, static_cast<off_t>(size)) != 0) {
        throw util::Error(std::string{"truncate failed: "} + std::strerror(errno));
    }
}

void FileHandle::sync(bool dataOnly) const
{
    const int result = dataOnly ? fdatasync(fd_) : fsync(fd_);
    if (result != 0) {
        throw util::Error(std::string{"sync failed: "} + std::strerror(errno));
    }
}

} // namespace virtualvaultfs::vfs
