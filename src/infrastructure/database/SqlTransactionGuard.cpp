#include "SqlTransactionGuard.h"

#include "SqlErrorMapper.h"

#include <QThread>

namespace devhub::infrastructure::database {

SqlTransactionGuard::SqlTransactionGuard(QSqlDatabase database)
    : m_database(std::move(database)), m_ownerThread(QThread::currentThread())
{
    if (!m_database.isValid() || !m_database.isOpen()) {
        m_error = domain::errors::AppError::failure(
            domain::errors::ErrorCode::DatabaseUnavailable,
            domain::errors::ErrorCategory::Database,
            QStringLiteral("DevHub could not start a database transaction."),
            QStringLiteral("Transaction requested on a closed SQL connection."),
            QStringLiteral("begin database transaction"));
        return;
    }
    m_active = m_database.transaction();
    if (!m_active) {
        m_error = SqlErrorMapper::fromSqlError(m_database.lastError(),
                                               QStringLiteral("begin database transaction"));
    }
}

SqlTransactionGuard::~SqlTransactionGuard()
{
    if (!m_active) {
        return;
    }
    Q_ASSERT_X(QThread::currentThread() == m_ownerThread,
               "SqlTransactionGuard",
               "A SQL transaction must be rolled back in its owning thread.");
    if (QThread::currentThread() == m_ownerThread) {
        m_database.rollback();
    }
}

shared::common::Result<void> SqlTransactionGuard::commit()
{
    if (QThread::currentThread() != m_ownerThread) {
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::DatabaseQueryFailed,
            domain::errors::ErrorCategory::Database,
            QStringLiteral("DevHub could not finish a database operation."),
            QStringLiteral("SQL transaction commit was called from outside its owning thread."),
            QStringLiteral("commit database transaction")));
    }
    if (!m_active) {
        return shared::common::Result<void>::failure(
            m_error.value_or(domain::errors::AppError::failure(
                domain::errors::ErrorCode::DatabaseQueryFailed,
                domain::errors::ErrorCategory::Database,
                QStringLiteral("DevHub could not finish a database operation."),
                QStringLiteral("Attempted to commit a transaction that was not active."),
                QStringLiteral("commit database transaction"))));
    }
    if (!m_database.commit()) {
        m_error = SqlErrorMapper::fromSqlError(m_database.lastError(),
                                               QStringLiteral("commit database transaction"));
        return shared::common::Result<void>::failure(*m_error);
    }
    m_active = false;
    return shared::common::Result<void>::success();
}

} // namespace devhub::infrastructure::database
