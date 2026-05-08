#include "db/Database.hpp"

#include "util/Error.hpp"

#include <sqlite3.h>
#include <utility>

namespace virtualvaultfs::db {

Database::Database(std::filesystem::path path)
    : path_(std::move(path))
{
    if (sqlite3_open(path_.string().c_str(), &handle_) != SQLITE_OK) {
        std::string message = "failed to open database: ";
        message += sqlite3_errmsg(handle_);
        sqlite3_close(handle_);
        handle_ = nullptr;
        throw util::Error(message);
    }
}

Database::~Database()
{
    sqlite3_close(handle_);
}

Database::Database(Database&& other) noexcept
    : path_(std::move(other.path_))
    , handle_(std::exchange(other.handle_, nullptr))
{
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other) {
        sqlite3_close(handle_);
        path_ = std::move(other.path_);
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

const std::filesystem::path& Database::path() const
{
    return path_;
}

sqlite3* Database::handle() const
{
    return handle_;
}

void Database::exec(const std::string& sql) const
{
    char* error = nullptr;
    if (sqlite3_exec(handle_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = "database exec failed: ";
        message += error != nullptr ? error : "unknown error";
        sqlite3_free(error);
        throw util::Error(message);
    }
}

std::int64_t Database::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(handle_);
}

} // namespace virtualvaultfs::db
