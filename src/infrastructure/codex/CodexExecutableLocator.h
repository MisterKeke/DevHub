#pragma once

#include "application/assistant/AssistantTypes.h"
#include "application/assistant/ICodexExecutableProbe.h"
#include "domain/errors/AppError.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <optional>

namespace devhub::infrastructure::codex {
class CodexExecutableLocator final : public application::assistant::ICodexExecutableProbe
{
    Q_OBJECT
public:
    explicit CodexExecutableLocator(QObject* parent = nullptr, int timeoutMilliseconds = 5000,
        QStringList testedVersions = {QStringLiteral("0.160.1")});
    ~CodexExecutableLocator() override;
    void locate(const QString& configuredPath, quint64 probeGeneration) override;
    void cancel() override;
    [[nodiscard]] bool isProbing() const override { return m_active; }
private:
    void readOutput();
    void fail(domain::errors::ErrorCode code, const QString& message);
    void restartQueuedProbe();
    QProcess m_process;
    QTimer m_timer;
    QStringList m_testedVersions;
    QString m_path;
    QByteArray m_output;
    quint64 m_generation = 0;
    int m_timeoutMilliseconds;
    bool m_active = false;
    bool m_failed = false;
    qsizetype m_stderrBytes = 0;
    std::optional<std::pair<QString, quint64>> m_queuedProbe;
};
} // namespace devhub::infrastructure::codex
