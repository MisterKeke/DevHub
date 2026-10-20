#include "CodexProcess.h"
#include "CodexSchemaInspector.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace devhub::infrastructure::codex {
using shared::common::Result;
namespace {
domain::errors::AppError processError(domain::errors::ErrorCode code, const QString& message)
{
    return domain::errors::AppError::failure(code, domain::errors::ErrorCategory::Process,
        message, {}, QStringLiteral("codex.transport"));
}
}
CodexProcess::CodexProcess(QObject* parent, application::assistant::CodexTransportLimits limits)
    : ICodexTransport(parent), m_limits(limits), m_codec(limits), m_writeQueue(limits.stdinBytes)
{
    m_startTimer.setSingleShot(true);
    m_stopTimer.setSingleShot(true);
    m_writeRetryTimer.setSingleShot(true);
    m_writeRetryTimer.setInterval(10);
    connect(&m_writeRetryTimer, &QTimer::timeout, this, &CodexProcess::flushWrites);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(&m_process, &QProcess::started, this, [this] {
        m_startTimer.stop();
        if (m_stopping) {
            if (m_stopStage > 0) m_process.kill();
            else flushWrites();
            return;
        }
        emit started(m_generation);
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CodexProcess::schedulePump);
    connect(&m_process, &QProcess::readyReadStandardError, this, &CodexProcess::schedulePump);
    connect(&m_process, &QProcess::bytesWritten, this, [this](qint64) { flushWrites(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_stopping && error == QProcess::Crashed) return; // Expected terminate/kill escalation.
        reportFailure(processError(error == QProcess::FailedToStart
            ? domain::errors::ErrorCode::StartupFailed : domain::errors::ErrorCode::IoFailure,
            QStringLiteral("The Assistant process failed.")));
        if (error == QProcess::FailedToStart) {
            m_exitCode = -1; m_exitPending = true; schedulePump();
        } else stop();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            m_exitCode = code; m_crashed = status == QProcess::CrashExit;
            m_exitPending = true; m_startTimer.stop(); m_stopTimer.stop(); schedulePump();
        });
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        reportFailure(processError(domain::errors::ErrorCode::StartupFailed,
            QStringLiteral("The Assistant process start timed out.")));
        stop();
    });
    connect(&m_stopTimer, &QTimer::timeout, this, [this] {
        if (m_process.state() == QProcess::NotRunning) return;
        if (m_stopStage++ == 0) {
            m_process.terminate(); m_stopTimer.start(m_limits.terminateMilliseconds);
        } else m_process.kill();
    });
}
CodexProcess::~CodexProcess()
{
    // Normal owners await stopped with a live event loop before destruction.
    // Emergency fallback: never enter QProcess's default 30-second wait first.
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(100);
    }
}
void CodexProcess::launch(const application::assistant::CodexLaunchSpec& spec)
{
    if (m_active) {
        emit failed(processError(domain::errors::ErrorCode::ConcurrencyLimitReached,
            QStringLiteral("The Assistant process is already active.")), spec.processGeneration);
        return;
    }
    m_generation = spec.processGeneration;
    m_active = true; m_stopping = false; m_failed = false; m_exitPending = false;
    m_crashed = false; m_stopStage = 0; m_discardStderr = false;
    m_codec.reset(); m_writeQueue.clear(); m_stderrFragment.clear(); m_stderrRing.clear();
    const QFileInfo executable(spec.executable.path);
    const QFileInfo directory(spec.scratchDirectory);
    if (!executable.isAbsolute() || !executable.isFile()
        || (!spec.executable.compatibility.binaryIdentity.isEmpty()
            && !CodexSchemaInspector::matchesBinaryIdentity(spec.executable.path, spec.executable.compatibility.binaryIdentity))
        || !directory.isAbsolute() || !directory.isDir()
        || spec.environment.inheritsFromParent()) {
        reportFailure(processError(domain::errors::ErrorCode::InvalidArgument,
            QStringLiteral("The Assistant launch specification is invalid.")));
        m_exitCode = -1; m_exitPending = true; schedulePump(); return;
    }
    m_process.setProgram(spec.executable.path);
    m_process.setArguments(spec.arguments);
    m_process.setWorkingDirectory(spec.scratchDirectory);
    m_process.setProcessEnvironment(spec.environment);
    m_startTimer.start(m_limits.startMilliseconds);
    m_process.start();
}
qint64 CodexProcess::pendingWriteBytes() const
{
    return m_writeQueue.size() + m_process.bytesToWrite();
}
Result<void> CodexProcess::sendObject(const QJsonObject& object)
{
    if (!m_active || m_stopping || m_failed || m_process.state() != QProcess::Running)
        return Result<void>::failure(processError(domain::errors::ErrorCode::ShutdownInProgress,
            QStringLiteral("The Assistant transport is not accepting work.")));
    auto encoded = m_codec.encode(object);
    if (!encoded) return Result<void>::failure(encoded.error());
    const auto queued = m_writeQueue.enqueue(encoded.value(), m_process.bytesToWrite());
    if (!queued) return queued;
    flushWrites();
    return Result<void>::success();
}
void CodexProcess::flushWrites()
{
    if (m_process.state() != QProcess::Running) return;
    const auto accepted = m_writeQueue.flush([this](const char* bytes, qint64 size) {
        return m_process.write(bytes, (std::min)(size, m_limits.writeChunkBytes));
    });
    if (!accepted) {
        m_writeQueue.clear(); m_writeRetryTimer.stop();
        reportFailure(accepted.error()); stop(); return;
    }
    // A zero write may produce no bytesWritten signal. Retry with a timer so
    // backpressure neither strands the suffix nor spins the GUI event loop.
    if (accepted.value() == 0 && m_writeQueue.size() > 0) m_writeRetryTimer.start();
    else m_writeRetryTimer.stop();
    if (m_stopping && pendingWriteBytes() == 0) m_process.closeWriteChannel();
}
void CodexProcess::stop()
{
    if (!m_active || m_stopping) return;
    m_stopping = true;
    m_startTimer.stop();
    flushWrites();
    m_stopTimer.start(m_limits.drainMilliseconds);
}
void CodexProcess::reportFailure(const domain::errors::AppError& error)
{
    if (!m_active || m_failed) return;
    m_failed = true;
    emit failed(error, m_generation);
}
void CodexProcess::schedulePump()
{
    if (!m_active || m_pumpQueued) return;
    m_pumpQueued = true;
    QTimer::singleShot(0, this, [this] { m_pumpQueued = false; if (m_active) pump(); });
}
void CodexProcess::consumeStderr(const QByteArray& bytes)
{
    for (const auto byte : bytes) {
        if (byte == '\n') {
            if (!m_discardStderr) {
                auto line = m_redactor.redact(QString::fromUtf8(m_stderrFragment));
                for (auto& ch : line) if (ch.unicode() < 32 || ch.unicode() == 127) ch = QChar(' ');
                auto safe = line.toUtf8(); safe.append('\n');
                if (safe.size() <= m_limits.stderrBytes) {
                    m_stderrRing.append(safe);
                    while (m_stderrRing.size() > m_limits.stderrBytes)
                        m_stderrRing.remove(0, m_stderrRing.indexOf('\n') + 1);
                }
            }
            m_stderrFragment.clear(); m_discardStderr = false;
        } else if (!m_discardStderr) {
            if (m_stderrFragment.size() >= m_limits.stderrBytes) {
                m_stderrFragment.clear(); m_discardStderr = true;
            } else m_stderrFragment.append(byte);
        }
    }
}
void CodexProcess::pump()
{
    QElapsedTimer budget; budget.start();
    int records = 0;
    do {
        // Give stderr progress even while stdout has a backlog. Read bounded chunks.
        m_process.setReadChannel(QProcess::StandardError);
        consumeStderr(m_process.read(4096));
        m_process.setReadChannel(QProcess::StandardOutput);
        if (!m_failed) {
            if (!m_codec.hasRecord() && m_process.bytesAvailable()) {
                const auto fed = m_codec.feed(m_process.read(64 * 1024));
                if (!fed) { reportFailure(fed.error()); stop(); }
            }
            if (!m_failed && m_codec.hasRecord()) {
                const auto envelope = m_codec.nextEnvelope();
                ++records;
                if (!envelope) { reportFailure(envelope.error()); stop(); }
                else if (envelope.value()) emit objectReceived(*envelope.value(), m_generation);
            }
        } else m_process.read(64 * 1024); // Discard corrupt stdout while stopping.
        m_process.setReadChannel(QProcess::StandardError);
        const bool stderrPending = m_process.bytesAvailable() > 0;
        m_process.setReadChannel(QProcess::StandardOutput);
        if (!stderrPending && !m_process.bytesAvailable() && (m_failed || !m_codec.hasRecord())) {
            if (m_exitPending) completeExit();
            return;
        }
    } while (records < m_limits.recordsPerDispatch && budget.elapsed() < m_limits.dispatchMilliseconds);
    schedulePump();
}
void CodexProcess::completeExit()
{
    if (!m_failed) {
        const auto eof = m_codec.finish();
        if (!eof) reportFailure(eof.error());
        else if (!m_stopping && m_exitCode != 0)
            reportFailure(processError(domain::errors::ErrorCode::IoFailure,
                QStringLiteral("The Assistant process exited unexpectedly.")));
    }
    m_startTimer.stop(); m_stopTimer.stop(); m_writeRetryTimer.stop();
    m_writeQueue.clear(); m_stderrFragment.clear();
    m_exitPending = false; m_active = false;
    emit stopped(m_exitCode, m_crashed, m_generation);
}
} // namespace devhub::infrastructure::codex
