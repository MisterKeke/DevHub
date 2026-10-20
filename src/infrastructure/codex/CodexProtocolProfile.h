#pragma once
#include <QStringList>

namespace devhub::infrastructure::codex {
// Wire snapshots are adapter implementation data, not a release whitelist.
struct CodexProtocolProfile final {
    QString id;
    QString snapshotVersion;
    QString bindingSemantics;
    bool experimentalApi = true; // historyMode and environment suppression require it
    bool dynamicTools = true;
    bool initialTurnsPage = false;
    bool summarizedLiveItems = false;
    bool scratchEnvironmentOnResume = false;
    [[nodiscard]] static QList<CodexProtocolProfile> profiles();
    [[nodiscard]] static CodexProtocolProfile named(const QString& id);
    // Only for existing offline fixtures and explicitly scoped behavioral evidence.
    [[nodiscard]] static CodexProtocolProfile fixture(const QString& version);
};
}
