#pragma once
#include "application/assistant/CodexCompatibility.h"
#include <QJsonObject>

namespace devhub::infrastructure::codex {
// A conservative schema comparator, independent of the runtime value validator.
// Input is a map of relative JSON file names to parsed schema documents.
class CodexSchemaSurface final {
public:
    [[nodiscard]] static application::assistant::CodexCompatibilityReport inspect(const QJsonObject& files);
    [[nodiscard]] static QString identity(const QJsonObject& files);
};
}
