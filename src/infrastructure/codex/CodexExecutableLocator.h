#pragma once

#include "application/assistant/AssistantTypes.h"
#include "domain/errors/AppError.h"
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace devhub::infrastructure::codex {
class CodexExecutableLocator final : public QObject
{
    Q_OBJECT
public:
    explicit CodexExecutableLocator(QObject* parent = nullptr, int timeoutMilliseconds = 5000,
        QStringList testedVersions = {QStringLiteral("0.160.1")});
    ~CodexExecutableLocator() override;
    void locate(const QString& configuredPath, quint64 probeGeneration);
    void cancel();
    [[nodiscard]] bool isProbing() const { return m_active; }
signals:
    void executableFound(devhub::application::assistant::CodexExecutable executable, quint64 probeGeneration);
    void probeFailed(devhub::domain::errors::AppError error, quint64 probeGeneration);
private:
    void readOutput();
    void fail(domain::errors::ErrorCode code, const QString& message);
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
};
} // namespace devhub::infrastructure::codex
