#pragma once

#include "../../domain/errors/AppError.h"

#include <QSqlError>
#include <QString>

namespace devhub::infrastructure::database {

class SqlErrorMapper final
{
public:
    [[nodiscard]] static domain::errors::AppError fromSqlError(const QSqlError& error,
                                                               const QString& operation,
                                                               bool migration = false);
};

} // namespace devhub::infrastructure::database
