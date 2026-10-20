#include "CodexExecutableLocator.h"
#include "CodexVersionSupport.h"
#include <QFileInfo>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace devhub::infrastructure::codex {
CodexExecutableProbeWorker::CodexExecutableProbeWorker(int timeoutMilliseconds)
    : ICodexExecutableProbe(nullptr), m_timeoutMilliseconds(timeoutMilliseconds)
{
    m_process.setParent(this);
    m_timer.setParent(this);
    m_schemaInspector.setParent(this);
    m_timer.setSingleShot(true);
    connect(&m_schemaInspector, &CodexSchemaInspector::completed, this,
        [this](const application::assistant::CodexCompatibilityReport& report) {
            m_active = false;
            if (m_failed) { restartQueuedProbe(); return; }
            m_compatibility = report;
            if (!report.protocolCompatible) {
                fail(domain::errors::ErrorCode::InvalidArgument, report.explanation); return;
            }
            emit compatibilityChecked(report, m_generation);
            emit executableFound({report.canonicalExecutable, report.reportedVersion, report}, m_generation);
        });
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
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CodexExecutableProbeWorker::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &CodexExecutableProbeWorker::readOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        fail(domain::errors::ErrorCode::StartupFailed, QStringLiteral("The Codex version probe failed."));
        if (error == QProcess::FailedToStart) { m_active = false; m_timer.stop(); restartQueuedProbe(); }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            m_processTree.close();
            readOutput(); m_timer.stop(); m_active = false;
            if (m_failed) { restartQueuedProbe(); return; }
            const auto parsedVersion = CodexVersionSupport::parseVersionResponse(QString::fromUtf8(m_output));
            if (code != 0 || status != QProcess::NormalExit || !parsedVersion) {
                fail(domain::errors::ErrorCode::StartupFailed, QStringLiteral("Codex returned an invalid version response.")); return;
            }
            const auto version = *parsedVersion;
            m_compatibility.reportedVersion = version;
            if (m_binaryIdentity.isEmpty() || CodexSchemaInspector::binaryIdentity(m_path) != m_binaryIdentity) {
                m_compatibility.reason = application::assistant::CodexCompatibilityReason::ExecutableChanged;
                m_compatibility.state = application::assistant::CodexCompatibilityState::Incompatible;
                fail(domain::errors::ErrorCode::StartupFailed, QStringLiteral("The Codex executable changed during version discovery.")); return;
            }
            m_active = true;
            m_schemaInspector.inspect(m_path, version);
        });
}
CodexExecutableProbeWorker::~CodexExecutableProbeWorker()
{
    disconnect(&m_process, nullptr, this, nullptr);
    m_processTree.close();
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(100); }
}
void CodexExecutableProbeWorker::locate(const QString& configuredPath, quint64 probeGeneration)
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
    m_generation = probeGeneration; m_failed = false; m_output.clear(); m_stderrBytes = 0; m_compatibility = {};
    // PATH was resolved by the facade using the caller's environment.
    const QString path = configuredPath;
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
    m_compatibility.canonicalExecutable = m_path;
    m_binaryIdentity = CodexSchemaInspector::binaryIdentity(m_path);
    m_compatibility.binaryIdentity = m_binaryIdentity;
    if (m_binaryIdentity.isEmpty()) {
        fail(domain::errors::ErrorCode::PathUnavailable, QStringLiteral("The Codex executable could not be identified.")); return;
    }
    m_process.setProgram(m_path);
    m_process.setArguments({QStringLiteral("--version")});
    m_process.setWorkingDirectory(file.absolutePath());
    m_process.setProcessEnvironment(application::assistant::codexBaseEnvironment(QProcessEnvironment::systemEnvironment()));
    if (!m_processTree.prepare(m_process)) {
        fail(domain::errors::ErrorCode::StartupFailed, QStringLiteral("Bounded executable inspection is unavailable on this system.")); return;
    }
    m_active = true; m_timer.start(m_timeoutMilliseconds); m_process.start();
}
void CodexExecutableProbeWorker::readOutput()
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
void CodexExecutableProbeWorker::fail(domain::errors::ErrorCode code, const QString& message)
{
    if (m_failed) return;
    m_failed = true;
    using application::assistant::CodexCompatibilityReason;
    if (m_compatibility.reason == CodexCompatibilityReason::EvidenceMissing) {
        m_compatibility.reason = code == domain::errors::ErrorCode::OperationCancelled ? CodexCompatibilityReason::Cancelled
            : code == domain::errors::ErrorCode::PathUnavailable ? CodexCompatibilityReason::ExecutableMissing : CodexCompatibilityReason::InvalidVersion;
        m_compatibility.state = code == domain::errors::ErrorCode::OperationCancelled
            ? application::assistant::CodexCompatibilityState::VerificationRequired : application::assistant::CodexCompatibilityState::Incompatible;
        m_compatibility.explanation = message;
    }
    m_compatibility.explanation = message;
    emit compatibilityChecked(m_compatibility, m_generation);
    emit probeFailed(domain::errors::AppError::failure(code, domain::errors::ErrorCategory::Process,
        message, {}, QStringLiteral("codex.probe")), m_generation);
}
void CodexExecutableProbeWorker::cancel()
{
    m_queuedProbe.reset();
    if (!m_active) return;
    m_timer.stop();
    fail(domain::errors::ErrorCode::OperationCancelled, QStringLiteral("The Codex version probe was cancelled."));
    m_schemaInspector.cancel();
    m_processTree.close();
    m_process.kill();
}
void CodexExecutableProbeWorker::restartQueuedProbe()
{
    QTimer::singleShot(0, this, [this] {
        if (m_active || !m_queuedProbe) return;
        const auto queued = std::move(*m_queuedProbe); m_queuedProbe.reset();
        locate(queued.first, queued.second);
    });
}

CodexExecutableLocator::CodexExecutableLocator(QObject* parent, int timeoutMilliseconds)
    : ICodexExecutableProbe(parent), m_worker(new CodexExecutableProbeWorker(timeoutMilliseconds))
{
    qRegisterMetaType<application::assistant::CodexCompatibilityReport>();
    qRegisterMetaType<application::assistant::CodexExecutable>();
    qRegisterMetaType<domain::errors::AppError>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &ICodexExecutableProbe::compatibilityChecked, this,
        [this](const application::assistant::CodexCompatibilityReport& report, quint64 generation) {
            if (generation == m_generation && !m_cancelled) emit compatibilityChecked(report, generation);
        });
    connect(m_worker, &ICodexExecutableProbe::executableFound, this,
        [this](const application::assistant::CodexExecutable& executable, quint64 generation) {
            if (generation != m_generation || m_cancelled) return;
            m_active = false; emit executableFound(executable, generation);
        });
    connect(m_worker, &ICodexExecutableProbe::probeFailed, this,
        [this](const domain::errors::AppError& error, quint64 generation) {
            if (generation != m_generation || m_cancelled) return;
            m_active = false; emit probeFailed(error, generation);
        });
    m_thread.setObjectName(QStringLiteral("Codex executable inspection"));
    m_thread.start();
}
CodexExecutableLocator::~CodexExecutableLocator()
{
    m_thread.requestInterruption();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] {
        worker->cancel(); QThread::currentThread()->quit();
    }, Qt::QueuedConnection);
    m_thread.wait();
}
void CodexExecutableLocator::locate(const QString& configuredPath, quint64 probeGeneration)
{
    // Resolve PATH at the call boundary, before an asynchronous caller restores
    // or changes its environment. Reading/hash/comparison happen on the worker.
    QString path = configuredPath;
    if (path.isEmpty()) {
#ifdef Q_OS_WIN
        path = QStandardPaths::findExecutable(QStringLiteral("codex.exe"));
#else
        path = QStandardPaths::findExecutable(QStringLiteral("codex"));
#endif
    }
    m_generation = probeGeneration; m_active = true; m_cancelled = false;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, path, probeGeneration] {
        worker->locate(path, probeGeneration);
    }, Qt::QueuedConnection);
}
void CodexExecutableLocator::cancel()
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->cancel(); }, Qt::QueuedConnection);
    if (!m_active) return;
    // Invalidate queued success immediately, including completion that the worker
    // has already posted but the UI has not yet received.
    m_cancelled = true; m_active = false;
    application::assistant::CodexCompatibilityReport report;
    report.reason = application::assistant::CodexCompatibilityReason::Cancelled;
    report.explanation = QStringLiteral("The Codex version probe was cancelled.");
    emit compatibilityChecked(report, m_generation);
    emit probeFailed(domain::errors::AppError::failure(domain::errors::ErrorCode::OperationCancelled,
        domain::errors::ErrorCategory::Process, report.explanation, {}, QStringLiteral("codex.probe")), m_generation);
}
} // namespace devhub::infrastructure::codex
