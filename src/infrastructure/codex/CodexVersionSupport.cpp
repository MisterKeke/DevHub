#include "CodexVersionSupport.h"

#include <QRegularExpression>

namespace devhub::infrastructure::codex {

QStringList CodexVersionSupport::supportedVersions()
{
    return {QStringLiteral("0.160.1"), QStringLiteral("0.162.0-alpha.17.2")};
}

bool CodexVersionSupport::isValidVersion(const QString& version)
{
    static const QRegularExpression expression(
        QStringLiteral("\\A(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
                       "(?:-(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
                       "(?:\\.(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*)?"
                       "(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?\\z"));
    return expression.match(version).hasMatch();
}

std::optional<QString> CodexVersionSupport::parseVersionResponse(const QString& response)
{
    static const QRegularExpression output(
        QStringLiteral("\\Acodex-cli ((?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)"
                       "(?:-(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
                       "(?:\\.(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*)?"
                       "(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?)\\r?\\n?\\z"));
    const auto match = output.match(response);
    if (!match.hasMatch() || !isValidVersion(match.captured(1))) return std::nullopt;
    return match.captured(1);
}

bool CodexVersionSupport::isSupported(const QString& version)
{
    return isValidVersion(version) && supportedVersions().contains(version);
}

QString CodexVersionSupport::unsupportedMessage(const QString& detectedVersion)
{
    return QStringLiteral("Codex version %1 has not been tested by DevHub. Supported versions: %2.")
        .arg(detectedVersion, supportedVersions().join(QStringLiteral(", ")));
}

} // namespace devhub::infrastructure::codex
