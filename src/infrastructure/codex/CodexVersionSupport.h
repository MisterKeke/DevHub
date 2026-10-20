#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace devhub::infrastructure::codex {

class CodexVersionSupport final {
public:
    // Diagnostic fixture releases only; compatibility is established separately.
    [[nodiscard]] static QStringList fixtureVersions();
    [[nodiscard]] static bool isValidVersion(const QString& version);
    [[nodiscard]] static std::optional<QString> parseVersionResponse(const QString& response);
};

} // namespace devhub::infrastructure::codex
