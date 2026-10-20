#include "CodexProtocolProfile.h"

namespace devhub::infrastructure::codex {
QList<CodexProtocolProfile> CodexProtocolProfile::profiles()
{
    // Both adapters enforce the same account, home, policy, tool and complete
    // legacy-history semantics. Only the resume transport and live views differ.
    return {{"legacy-v1", "0.160.1", "restricted-legacy-history-v2", true, true, false, false, false},
        {"paged-resume-v1", "0.162.0-alpha.17.2", "restricted-legacy-history-v2", true, true, true, true, true}};
}
CodexProtocolProfile CodexProtocolProfile::named(const QString& id)
{
    for (const auto& profile : profiles()) if (profile.id == id) return profile;
    return {};
}
CodexProtocolProfile CodexProtocolProfile::fixture(const QString& version)
{
    for (const auto& profile : profiles()) if (profile.snapshotVersion == version) return profile;
    return {};
}
}
