#pragma once

#include "db/Database.hpp"

namespace virtualvaultfs::db {

class Transaction {
public:
    explicit Transaction(const Database& database);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    bool committed() const;
    void commit();

private:
    const Database* database_{nullptr};
    bool committed_{false};
};

} // namespace virtualvaultfs::db
