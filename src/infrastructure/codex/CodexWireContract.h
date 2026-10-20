#pragma once
#include <QJsonObject>
#include "CodexProtocolProfile.h"

namespace devhub::infrastructure::codex {
// Structural keywords in the pinned Phase 0 projection. Formats/defaults are
// annotations; profile policy supplies the additional semantic checks.
class CodexWireContract final {
public:
    explicit CodexWireContract(QString version = QStringLiteral("0.160.1"));
    [[nodiscard]] bool setVersion(const QString& version);
    [[nodiscard]] bool setProfile(const QString& adapter, const QString& diagnosticVersion);
    [[nodiscard]] CodexProtocolProfile profile() const { return m_profile; }
    [[nodiscard]] QJsonObject snapshot() const { return {{"contracts", m_contracts}, {"definitions", m_definitions}}; }
    [[nodiscard]] QString version() const { return m_version; }
    [[nodiscard]] bool accepts(const QString& contract, const QJsonValue& value) const;
    [[nodiscard]] bool acceptsDefinition(const QString& definition, const QJsonValue& value) const;
    [[nodiscard]] bool contains(const QString& contract) const;
private:
    [[nodiscard]] bool validate(const QJsonValue& schema, const QJsonValue& value, int depth, int& budget, bool informationalExtras = false) const;
    QJsonObject m_contracts;
    QJsonObject m_definitions;
    QString m_version;
    CodexProtocolProfile m_profile;
};
}
