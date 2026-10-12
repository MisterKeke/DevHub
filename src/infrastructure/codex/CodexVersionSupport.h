#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace devhub::infrastructure::codex {

class CodexVersionSupport final {
public:
    [[nodiscard]] static QStringList supportedVersions();
    [[nodiscard]] static bool isValidVersion(const QString& version);
    [[nodiscard]] static std::optional<QString> parseVersionResponse(const QString& response);
    [[nodiscard]] static bool isSupported(const QString& version);
    [[nodiscard]] static QString unsupportedMessage(const QString& detectedVersion);
};

} // namespace devhub::infrastructure::codex
