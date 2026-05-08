#include "db/Statement.hpp"

#include "util/Error.hpp"

#include <sqlite3.h>
#include <utility>

namespace virtualvaultfs::db {

namespace {

void checkSqlite(int result, sqlite3* handle)
{
    if (result != SQLITE_OK) {
        throw util::Error(sqlite3_errmsg(handle));
    }
}

} // namespace

Statement::Statement(const Database& database, std::string sql)
    : sql_(std::move(sql))
{
    checkSqlite(sqlite3_prepare_v2(database.handle(), sql_.c_str(), -1, &statement_, nullptr), database.handle());
}

Statement::~Statement()
{
    sqlite3_finalize(statement_);
}

Statement::Statement(Statement&& other) noexcept
    : sql_(std::move(other.sql_))
    , statement_(std::exchange(other.statement_, nullptr))
{
}

Statement& Statement::operator=(Statement&& other) noexcept
{
    if (this != &other) {
        sqlite3_finalize(statement_);
        sql_ = std::move(other.sql_);
        statement_ = std::exchange(other.statement_, nullptr);
    }
    return *this;
}

const std::string& Statement::sql() const
{
    return sql_;
}

void Statement::bind(int index, std::int64_t value)
{
    if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK) {
        throw util::Error("failed to bind integer parameter");
    }
}

void Statement::bind(int index, std::string_view value)
{
    if (sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw util::Error("failed to bind text parameter");
    }
}

void Statement::bindNull(int index)
{
    if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
        throw util::Error("failed to bind null parameter");
    }
}

bool Statement::step()
{
    const int result = sqlite3_step(statement_);
    if (result == SQLITE_ROW) {
        return true;
    }
    if (result == SQLITE_DONE) {
        return false;
    }
    throw util::Error("statement step failed");
}

void Statement::exec()
{
    (void)step();
}

void Statement::reset()
{
    sqlite3_reset(statement_);
    sqlite3_clear_bindings(statement_);
}

std::int64_t Statement::columnInt64(int index) const
{
    return sqlite3_column_int64(statement_, index);
}

std::optional<std::int64_t> Statement::columnOptionalInt64(int index) const
{
    if (sqlite3_column_type(statement_, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int64(statement_, index);
}

std::string Statement::columnText(int index) const
{
    const auto* text = sqlite3_column_text(statement_, index);
    if (text == nullptr) {
        return {};
    }
    return reinterpret_cast<const char*>(text);
}

} // namespace virtualvaultfs::db
