#pragma once

#include "../../domain/errors/AppError.h"
#include "../../shared/common/Result.h"

#include <QSqlDatabase>
#include <QThread>

#include <optional>

namespace devhub::infrastructure::database {

class SqlTransactionGuard final
{
public:
    explicit SqlTransactionGuard(QSqlDatabase database);
    ~SqlTransactionGuard();
    SqlTransactionGuard(const SqlTransactionGuard&) = delete;
    SqlTransactionGuard& operator=(const SqlTransactionGuard&) = delete;
    SqlTransactionGuard(SqlTransactionGuard&&) = delete;
    SqlTransactionGuard& operator=(SqlTransactionGuard&&) = delete;

    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] const std::optional<domain::errors::AppError>& error() const noexcept { return m_error; }
    [[nodiscard]] shared::common::Result<void> commit();

private:
    QSqlDatabase m_database;
    QThread* m_ownerThread = nullptr;
    bool m_active = false;
    std::optional<domain::errors::AppError> m_error;
};

} // namespace devhub::infrastructure::database
