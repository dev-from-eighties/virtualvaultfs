#include "db/Transaction.hpp"

namespace virtualvaultfs::db {

Transaction::Transaction(const Database& database)
    : database_(&database)
{
    database_->exec("BEGIN IMMEDIATE");
}

Transaction::~Transaction()
{
    if (!committed_ && database_ != nullptr) {
        database_->exec("ROLLBACK");
    }
}

void Transaction::commit()
{
    database_->exec("COMMIT");
    committed_ = true;
}

bool Transaction::committed() const
{
    return committed_;
}

} // namespace virtualvaultfs::db
