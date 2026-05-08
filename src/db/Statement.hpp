#pragma once

#include "db/Database.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3_stmt;

namespace virtualvaultfs::db {

class Statement {
public:
    Statement(const Database& database, std::string sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    const std::string& sql() const;
    void bind(int index, std::int64_t value);
    void bind(int index, std::string_view value);
    void bindNull(int index);
    bool step();
    void exec();
    void reset();

    std::int64_t columnInt64(int index) const;
    std::optional<std::int64_t> columnOptionalInt64(int index) const;
    std::string columnText(int index) const;

private:
    std::string sql_;
    sqlite3_stmt* statement_{nullptr};
};

} // namespace virtualvaultfs::db
