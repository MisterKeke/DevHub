#include "RedactionService.h"

#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QReadLocker>
#include <QWriteLocker>

#include <utility>

namespace devhub::shared::security {
namespace {

const QRegularExpression& secretAssignmentPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("(?i)\\b(password|passwd|pwd|passphrase|token|access[_-]?token|"
                       "refresh[_-]?token|secret|client[_-]?secret|api[_-]?key|private[_-]?key)"
                       "\\b(\\s*[:=]\\s*)(?:\"[^\"]*\"|'[^']*'|[^\\s,;&]+)"));
    return pattern;
}

const QRegularExpression& authorizationPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("(?i)\\b(authorization\\s*:\\s*)(?:bearer|basic)\\s+[^\\s,;]+"));
    return pattern;
}

const QRegularExpression& urlCredentialPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("([A-Za-z][A-Za-z0-9+.-]*://)[^/@\\\\\\s:]+(?::[^/@\\\\\\s]*)?@"));
    return pattern;
}

QString redactNamedEnvironmentValues(QString text, const QSet<QString>& names)
{
    for (const QString& name : names) {
        if (name.isEmpty()) {
            continue;
        }
        const QString expression = QStringLiteral("(?i)\\b(%1)(\\s*=\\s*)(?:\"[^\"]*\"|'[^']*'|[^\\s,;&]+)")
                                       .arg(QRegularExpression::escape(name));
        text.replace(QRegularExpression(expression), QStringLiteral("\\1\\2[REDACTED]"));
    }
    return text;
}

} // namespace

void RedactionService::setSecretEnvironmentNames(QSet<QString> names)
{
    QWriteLocker lock(&m_lock);
    m_secretEnvironmentNames = std::move(names);
}

void RedactionService::addSecretEnvironmentName(QString name)
{
    if (name.isEmpty()) {
        return;
    }
    QWriteLocker lock(&m_lock);
    m_secretEnvironmentNames.insert(std::move(name));
}

void RedactionService::addSecretEnvironmentValue(QString value)
{
    if (value.size() < 8 || value.size() > 4096) return;
    QWriteLocker lock(&m_lock);
    m_secretEnvironmentValues.insert(std::move(value));
}

QString RedactionService::redact(QString text) const
{
    text.replace(urlCredentialPattern(), QStringLiteral("\\1[REDACTED]@"));
    text.replace(authorizationPattern(), QStringLiteral("\\1[REDACTED]"));
    text.replace(secretAssignmentPattern(), QStringLiteral("\\1\\2[REDACTED]"));

    QReadLocker lock(&m_lock);
    text = redactNamedEnvironmentValues(std::move(text), m_secretEnvironmentNames);
    for (const QString& value : m_secretEnvironmentValues) text.replace(value, QStringLiteral("[REDACTED]"));
    return text;
}

} // namespace devhub::shared::security
