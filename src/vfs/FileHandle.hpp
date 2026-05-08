#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace virtualvaultfs::vfs {

class FileHandle {
public:
    explicit FileHandle(int fd);
    ~FileHandle();

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&& other) noexcept;
    FileHandle& operator=(FileHandle&& other) noexcept;

    int fd() const;
    std::vector<std::byte> read(std::size_t size, std::int64_t offset) const;
    std::size_t write(std::span<const std::byte> data, std::int64_t offset) const;
    void truncate(std::int64_t size) const;
    void sync(bool dataOnly) const;

private:
    int fd_{-1};
};

} // namespace virtualvaultfs::vfs
