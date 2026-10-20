#include "CodexVersionSupport.h"
#include "CodexProtocolProfile.h"

#include <QRegularExpression>

namespace devhub::infrastructure::codex {

QStringList CodexVersionSupport::fixtureVersions()
{
    QStringList versions;
    for (const auto& profile : CodexProtocolProfile::profiles()) versions << profile.snapshotVersion;
    return versions;
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

} // namespace devhub::infrastructure::codex
