#include "SqlErrorMapper.h"

namespace devhub::infrastructure::database {

domain::errors::AppError SqlErrorMapper::fromSqlError(const QSqlError& error,
                                                      const QString& operation,
                                                      bool migration)
{
    using namespace domain::errors;
    const QString details = error.text().isEmpty() ? QStringLiteral("No driver details were provided.")
                                                   : error.text();
    const ErrorCategory category = migration ? ErrorCategory::Migration : ErrorCategory::Database;
    const ErrorCode code = error.type() == QSqlError::ConnectionError
        ? ErrorCode::DatabaseUnavailable
        : migration ? ErrorCode::MigrationFailed : ErrorCode::DatabaseQueryFailed;
    AppError result = AppError::failure(
        code,
        category,
        migration ? QStringLiteral("DevHub could not update its local database.")
                  : QStringLiteral("DevHub could not access its local database."),
        QStringLiteral("Qt SQL error %1 during %2: %3")
            .arg(static_cast<int>(error.type()))
            .arg(operation, details),
        operation);
    if (error.nativeErrorCode().toLongLong() != 0) {
        result.nativeCode = error.nativeErrorCode().toLongLong();
    }
    result.retryable = error.type() == QSqlError::ConnectionError
        || error.type() == QSqlError::TransactionError;
    return result;
}

} // namespace devhub::infrastructure::database
