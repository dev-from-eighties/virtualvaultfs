#pragma once

#include <filesystem>
#include <cstdint>
#include <string>

struct sqlite3;

namespace virtualvaultfs::db {

class Database {
public:
    explicit Database(std::filesystem::path path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    const std::filesystem::path& path() const;
    sqlite3* handle() const;
    void exec(const std::string& sql) const;
    std::int64_t lastInsertRowId() const;

private:
    std::filesystem::path path_;
    sqlite3* handle_{nullptr};
};

} // namespace virtualvaultfs::db
