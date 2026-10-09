#pragma once
#include <QJsonObject>

namespace devhub::infrastructure::codex {
// Structural keywords in the pinned Phase 0 projection. Formats/defaults are
// annotations; profile policy supplies the additional semantic checks.
class CodexWireContract final {
public:
    CodexWireContract();
    [[nodiscard]] bool accepts(const QString& contract, const QJsonValue& value) const;
    [[nodiscard]] bool contains(const QString& contract) const;
private:
    [[nodiscard]] bool validate(const QJsonValue& schema, const QJsonValue& value, int depth, int& budget) const;
    QJsonObject m_contracts;
    QJsonObject m_definitions;
};
}
