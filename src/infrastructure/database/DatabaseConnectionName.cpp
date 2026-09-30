#include "DatabaseConnectionName.h"

#include <QUuid>

namespace devhub::infrastructure::database {

DatabaseConnectionName::DatabaseConnectionName(QString value)
    : m_value(std::move(value))
{
}

DatabaseConnectionName DatabaseConnectionName::generate(const QString& purpose)
{
    return DatabaseConnectionName(QStringLiteral("devhub-%1-%2")
                                      .arg(purpose, QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

} // namespace devhub::infrastructure::database
