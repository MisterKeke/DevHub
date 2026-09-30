#pragma once

#include <QString>

namespace devhub::infrastructure::database {

class DatabaseConnectionName final
{
public:
    [[nodiscard]] static DatabaseConnectionName generate(const QString& purpose);
    [[nodiscard]] const QString& value() const noexcept { return m_value; }

private:
    explicit DatabaseConnectionName(QString value);
    QString m_value;
};

} // namespace devhub::infrastructure::database
