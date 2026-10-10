#include "CodexExecutableLocator.h"
#include "CodexVersionSupport.h"
#include <QFileInfo>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace devhub::infrastructure::codex {
CodexExecutableLocator::CodexExecutableLocator(QObject* parent, int timeoutMilliseconds)
    : ICodexExecutableProbe(parent), m_timeoutMilliseconds(timeoutMilliseconds)
{
    m_timer.setSingleShot(true);
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(&m_process, &QProcess::started, this, [this] { if (m_failed) m_process.kill(); });
    connect(&m_timer, &QTimer::timeout, this, [this] {
        fail(domain::errors::ErrorCode::StartupFailed, QStringLiteral("The Codex version probe timed out."));
        m_process.kill();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CodexExecutableLocator::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &CodexExecutableLocator::readOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        fail(domain::errors::ErrorCode::StartupFailed, QStringLiteral("The Codex version probe failed."));
        if (error == QProcess::FailedToStart) { m_active = false; m_timer.stop(); restartQueuedProbe(); }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            readOutput(); m_timer.stop(); m_active = false;
            if (m_failed) { restartQueuedProbe(); return; }
            const auto parsedVersion = CodexVersionSupport::parseVersionResponse(QString::fromUtf8(m_output));
            if (code != 0 || status != QProcess::NormalExit || !parsedVersion) {
                fail(domain::errors::ErrorCode::StartupFailed, QStringLiteral("Codex returned an invalid version response.")); return;
            }
            const auto version = *parsedVersion;
            if (!CodexVersionSupport::isSupported(version)) {
                fail(domain::errors::ErrorCode::InvalidArgument, CodexVersionSupport::unsupportedMessage(version)); return;
            }
            emit executableFound({m_path, version}, m_generation);
        });
}
CodexExecutableLocator::~CodexExecutableLocator()
{
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(100); }
}
void CodexExecutableLocator::locate(const QString& configuredPath, quint64 probeGeneration)
{
    if (m_active) {
        // Cancellation is asynchronous. A replacement probe must wait for the
        // owned QProcess to finish rather than racing its kill/finished events.
        if (m_failed) { m_queuedProbe.emplace(configuredPath, probeGeneration); return; }
        emit probeFailed(domain::errors::AppError::failure(domain::errors::ErrorCode::ConcurrencyLimitReached,
            domain::errors::ErrorCategory::Process, QStringLiteral("A Codex version probe is already running."),
            {}, QStringLiteral("codex.probe")), probeGeneration); return;
    }
    m_queuedProbe.reset();
    m_generation = probeGeneration; m_failed = false; m_output.clear(); m_stderrBytes = 0;
    QString path = configuredPath;
    if (path.isEmpty()) {
#ifdef Q_OS_WIN
        path = QStandardPaths::findExecutable(QStringLiteral("codex.exe"));
#else
        path = QStandardPaths::findExecutable(QStringLiteral("codex"));
#endif
    }
    const QFileInfo file(path);
    if (path.isEmpty()) { fail(domain::errors::ErrorCode::PathUnavailable, QStringLiteral("Native codex.exe was not found on PATH. Select its installed executable.")); return; }
    const auto suffix = file.suffix().toLower();
    if (path.isEmpty() || !file.isAbsolute() || !file.isFile() || !file.isExecutable()
        || suffix == QStringLiteral("cmd") || suffix == QStringLiteral("bat") || suffix == QStringLiteral("ps1")
#ifdef Q_OS_WIN
        || suffix != QStringLiteral("exe")
#endif
    ) {
        fail(domain::errors::ErrorCode::PathUnavailable, QStringLiteral("The Codex executable override is invalid. Select an installed native .exe; script wrappers and directories are unsupported.")); return;
    }
    m_path = file.canonicalFilePath();
    m_process.setProgram(m_path);
    m_process.setArguments({QStringLiteral("--version")});
    m_process.setWorkingDirectory(file.absolutePath());
    m_process.setProcessEnvironment(application::assistant::codexBaseEnvironment(QProcessEnvironment::systemEnvironment()));
    m_active = true; m_timer.start(m_timeoutMilliseconds); m_process.start();
}
void CodexExecutableLocator::readOutput()
{
    m_process.setReadChannel(QProcess::StandardOutput);
    const auto bytes = m_process.read(4097);
    if (m_output.size() + bytes.size() > 4096) {
        fail(domain::errors::ErrorCode::IoFailure, QStringLiteral("The Codex version response exceeded its limit."));
        m_process.kill();
    } else m_output.append(bytes);
    m_process.setReadChannel(QProcess::StandardError);
    m_stderrBytes += m_process.read(4097).size(); // Never retain startup warnings.
    if (m_stderrBytes > 4096) {
        fail(domain::errors::ErrorCode::IoFailure, QStringLiteral("The Codex version diagnostics exceeded their limit."));
        m_process.kill();
    }
    m_process.setReadChannel(QProcess::StandardOutput);
}
void CodexExecutableLocator::fail(domain::errors::ErrorCode code, const QString& message)
{
    if (m_failed) return;
    m_failed = true;
    emit probeFailed(domain::errors::AppError::failure(code, domain::errors::ErrorCategory::Process,
        message, {}, QStringLiteral("codex.probe")), m_generation);
}
void CodexExecutableLocator::cancel()
{
    m_queuedProbe.reset();
    if (!m_active) return;
    m_timer.stop();
    fail(domain::errors::ErrorCode::OperationCancelled, QStringLiteral("The Codex version probe was cancelled."));
    m_process.kill();
}
void CodexExecutableLocator::restartQueuedProbe()
{
    QTimer::singleShot(0, this, [this] {
        if (m_active || !m_queuedProbe) return;
        const auto queued = std::move(*m_queuedProbe); m_queuedProbe.reset();
        locate(queued.first, queued.second);
    });
}
} // namespace devhub::infrastructure::codex
