#pragma once
#include "application/assistant/AssistantTypes.h"
#include "CodexProcessTree.h"
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <optional>

namespace devhub::infrastructure::codex {
class CodexSchemaInspector final : public QObject {
    Q_OBJECT
public:
    explicit CodexSchemaInspector(QObject* parent = nullptr, int deadlineMilliseconds = 60000);
    ~CodexSchemaInspector() override;
    void inspect(const QString& path, const QString& version);
    void cancel();
    [[nodiscard]] static QString binaryIdentity(const QString& path);
    // Checks only a digest previously measured in this process. Never reads the
    // whole executable from UI admission/refresh paths; metadata changes or a
    // broken Windows read-cache lease revoke it without relying on timestamps.
    [[nodiscard]] static bool matchesBinaryIdentity(const QString& path, const QString& identity);
    // Only live acquisition can populate this process-local evidence registry.
    // A public value snapshot or an editable disk cache cannot grant admission.
    [[nodiscard]] static std::optional<application::assistant::CodexCompatibilityReport> evidence(
        const application::assistant::CodexExecutable& executable);
signals:
    void completed(application::assistant::CodexCompatibilityReport report);
private:
    void startMode();
    void drain();
    void checkBounds();
    void finish(application::assistant::CodexCompatibilityReport report);
    void fail(application::assistant::CodexCompatibilityReason reason, const QString& explanation);
    [[nodiscard]] QJsonObject readBundle();
    void cache(const QJsonObject& files, application::assistant::CodexCompatibilityReport& report);
    CodexProcessTree m_processTree;
    QProcess m_process;
    QTimer m_deadline;
    QTimer m_bounds;
    QElapsedTimer m_elapsed;
    std::unique_ptr<QTemporaryDir> m_directory;
    application::assistant::CodexCompatibilityReport m_report;
    bool m_active = false;
    qsizetype m_outputBytes = 0;
    int m_deadlineMilliseconds;
    std::optional<application::assistant::CodexCompatibilityReport> m_failure;
};
}
