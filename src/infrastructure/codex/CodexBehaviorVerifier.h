#pragma once
#include "application/assistant/AssistantTypes.h"
#include "CodexProcessTree.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <memory>

namespace devhub::infrastructure::codex {
// Explicit, offline verification using the embedded, reviewed synthetic harness.
// Reuse requires a current-user protected record plus fresh executable/schema
// and runtime checks. Plaintext settings and arbitrary verdict files cannot
// authorize a launch.
class CodexBehaviorVerifier final : public QObject {
    Q_OBJECT
public:
    explicit CodexBehaviorVerifier(QObject* parent = nullptr);
    ~CodexBehaviorVerifier() override;
    bool start(const application::assistant::CodexCompatibilityReport& report, const QJsonArray& tools);
    void cancel();
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] static QString failedCheckDescription(const QJsonObject& checks, bool toolInventory);
    [[nodiscard]] static QString evidenceIdentity(const application::assistant::CodexCompatibilityReport& report);
    [[nodiscard]] static bool cachedEvidenceMatches(const application::assistant::CodexCompatibilityReport& report);
    static void forgetCachedEvidence(const application::assistant::CodexCompatibilityReport& report);
signals:
    void completed(bool accepted, QString reason);
private:
    friend class CodexClient;
    static bool rememberVerifiedEvidence(const application::assistant::CodexCompatibilityReport& report);
    void runStage();
    void drain();
    void finish(bool accepted, const QString& reason);
    void closeProcessTree();
    CodexProcessTree m_processTree;
    QProcess m_process;
    QTimer m_deadline;
    QElapsedTimer m_elapsed;
    std::unique_ptr<QTemporaryDir> m_directory;
    application::assistant::CodexCompatibilityReport m_report;
    QString m_python;
    qsizetype m_outputBytes = 0;
    int m_stage = 0;
    bool m_active = false;
    bool m_cancelled = false;
    bool m_limitExceeded = false;
    bool m_verifyTools = false;
};
}
