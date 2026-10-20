#pragma once

#include "application/assistant/AssistantTypes.h"
#include "application/assistant/ICodexExecutableProbe.h"
#include "domain/errors/AppError.h"
#include "CodexSchemaInspector.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QThread>
#include <optional>

namespace devhub::infrastructure::codex {
// All process, filesystem and schema work belongs to this object's thread.
class CodexExecutableProbeWorker final : public application::assistant::ICodexExecutableProbe
{
    Q_OBJECT
public:
    explicit CodexExecutableProbeWorker(int timeoutMilliseconds);
    ~CodexExecutableProbeWorker() override;
    void locate(const QString& configuredPath, quint64 probeGeneration) override;
    void cancel() override;
    [[nodiscard]] bool isProbing() const override { return m_active; }
private:
    void readOutput();
    void fail(domain::errors::ErrorCode code, const QString& message);
    void restartQueuedProbe();
    CodexProcessTree m_processTree;
    QProcess m_process;
    CodexSchemaInspector m_schemaInspector;
    QTimer m_timer;
    QString m_path;
    QString m_binaryIdentity;
    application::assistant::CodexCompatibilityReport m_compatibility;
    QByteArray m_output;
    quint64 m_generation = 0;
    int m_timeoutMilliseconds;
    bool m_active = false;
    bool m_failed = false;
    qsizetype m_stderrBytes = 0;
    std::optional<std::pair<QString, quint64>> m_queuedProbe;
};

class CodexExecutableLocator final : public application::assistant::ICodexExecutableProbe
{
    Q_OBJECT
public:
    explicit CodexExecutableLocator(QObject* parent = nullptr, int timeoutMilliseconds = 5000);
    ~CodexExecutableLocator() override;
    void locate(const QString& configuredPath, quint64 probeGeneration) override;
    void cancel() override;
    [[nodiscard]] bool isProbing() const override { return m_active; }
private:
    QThread m_thread;
    CodexExecutableProbeWorker* m_worker;
    quint64 m_generation = 0;
    bool m_active = false;
    bool m_cancelled = false;
};
} // namespace devhub::infrastructure::codex
