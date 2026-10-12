#pragma once

#include "application/assistant/ICodexTransport.h"
#include "CodexProtocolCodec.h"
#include "CodexWriteQueue.h"
#include "shared/security/RedactionService.h"
#include <QProcess>
#include <QTimer>

namespace devhub::infrastructure::codex {
class CodexProcess final : public application::assistant::ICodexTransport
{
    Q_OBJECT
public:
    explicit CodexProcess(QObject* parent = nullptr,
        application::assistant::CodexTransportLimits limits = {});
    ~CodexProcess() override;
    void launch(const application::assistant::CodexLaunchSpec& spec) override;
    [[nodiscard]] shared::common::Result<void> sendObject(const QJsonObject& object) override;
    void stop() override;
    [[nodiscard]] qint64 processId() const { return m_process.processId(); }
    [[nodiscard]] qint64 pendingWriteBytes() const;
    // Transient, redacted complete lines only; never automatically persisted/logged.
    [[nodiscard]] QByteArray diagnosticPreview() const { return m_stderrRing; }
private:
    void schedulePump();
    void pump();
    void flushWrites();
    void consumeStderr(const QByteArray& bytes);
    void reportFailure(const domain::errors::AppError& error);
    void completeExit();
    QProcess m_process;
    QTimer m_startTimer;
    QTimer m_stopTimer;
    QTimer m_writeRetryTimer;
    application::assistant::CodexTransportLimits m_limits;
    CodexProtocolCodec m_codec;
    shared::security::RedactionService m_redactor;
    CodexWriteQueue m_writeQueue;
    QByteArray m_stderrFragment;
    QByteArray m_stderrRing;
    quint64 m_generation = 0;
    int m_stopStage = 0;
    int m_exitCode = 0;
    bool m_crashed = false;
    bool m_active = false;
    bool m_stopping = false;
    bool m_failed = false;
    bool m_pumpQueued = false;
    bool m_exitPending = false;
    bool m_discardStderr = false;
};
} // namespace devhub::infrastructure::codex
