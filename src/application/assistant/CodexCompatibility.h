#pragma once
#include <QStringList>
#include <QMetaType>

namespace devhub::application::assistant {
enum class CodexCompatibilityState { Compatible, VerificationRequired, Incompatible };
// Stable machine-readable codes; explanations must never contain raw peer output.
enum class CodexCompatibilityReason {
    None, ExecutableMissing, ExecutableChanged, InvalidVersion, SchemaCommandUnsupported,
    SchemaDeadline, SchemaLimit, SchemaMalformed, SchemaUnsupported, ProtocolRequirementMissing,
    Cancelled, RuntimeVerificationRequired, RuntimeRestrictionFailed, EvidenceMissing, ConfigurationChanged
};
struct CodexCompatibilityReport final {
    CodexCompatibilityState state = CodexCompatibilityState::VerificationRequired;
    CodexCompatibilityReason reason = CodexCompatibilityReason::EvidenceMissing;
    QString explanation = QStringLiteral("Compatibility has not been checked.");
    QString canonicalExecutable;
    QString reportedVersion;
    QString binaryIdentity;
    QString adapter;
    QString schemaIdentity;
    QString configurationIdentity;
    QString policyIdentity;
    QStringList evidence;
    bool protocolCompatible = false;
    bool runtimePolicyVerified = false;
    bool verificationInProgress = false;
    bool dynamicTools = false;
    bool resume = false;
    [[nodiscard]] QString reasonCode() const {
        switch (reason) {
        case CodexCompatibilityReason::None: return "compatible";
        case CodexCompatibilityReason::ExecutableMissing: return "executable.missing";
        case CodexCompatibilityReason::ExecutableChanged: return "executable.changed";
        case CodexCompatibilityReason::InvalidVersion: return "executable.invalid-version";
        case CodexCompatibilityReason::SchemaCommandUnsupported: return "schema.command-unsupported";
        case CodexCompatibilityReason::SchemaDeadline: return "schema.deadline";
        case CodexCompatibilityReason::SchemaLimit: return "schema.limit";
        case CodexCompatibilityReason::SchemaMalformed: return "schema.malformed";
        case CodexCompatibilityReason::SchemaUnsupported: return "schema.unsupported-construct";
        case CodexCompatibilityReason::ProtocolRequirementMissing: return "protocol.requirement-missing";
        case CodexCompatibilityReason::Cancelled: return "inspection.cancelled";
        case CodexCompatibilityReason::RuntimeVerificationRequired: return "runtime.verification-required";
        case CodexCompatibilityReason::RuntimeRestrictionFailed: return "runtime.restriction-failed";
        case CodexCompatibilityReason::EvidenceMissing: return "evidence.missing";
        case CodexCompatibilityReason::ConfigurationChanged: return "runtime.configuration-changed";
        }
        return "evidence.missing";
    }
    [[nodiscard]] QString stateText() const {
        switch (state) {
        case CodexCompatibilityState::Compatible: return "Compatible";
        case CodexCompatibilityState::VerificationRequired: return "Verification required";
        case CodexCompatibilityState::Incompatible: return "Incompatible";
        }
        return "Verification required";
    }
};
}
Q_DECLARE_METATYPE(devhub::application::assistant::CodexCompatibilityReport)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexCompatibilityReason)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexCompatibilityState)
