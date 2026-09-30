#pragma once

#include <QReadWriteLock>
#include <QSet>
#include <QHash>
#include <QString>

namespace devhub::shared::security {

class RedactionService final
{
public:
    void setSecretEnvironmentNames(QSet<QString> names);
    void addSecretEnvironmentName(QString name);
    void addSecretEnvironmentValue(QString value);
    [[nodiscard]] QString redact(QString text) const;

private:
    mutable QReadWriteLock m_lock;
    QSet<QString> m_secretEnvironmentNames;
    QSet<QString> m_secretEnvironmentValues;
};

} // namespace devhub::shared::security
