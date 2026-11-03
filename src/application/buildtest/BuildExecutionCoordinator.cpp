#include "BuildExecutionCoordinator.h"

#include "BuildResultParser.h"
#include "../../domain/errors/AppError.h"
#include "../workspaces/WorkspaceValidation.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QProcess>
#include <QRegularExpression>
#include <memory>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QUuid>

#include <algorithm>

namespace devhub::application::buildtest {
namespace {
constexpr qsizetype MaximumLogBytes = 4 * 1024 * 1024;
constexpr qsizetype MaximumStructuredBytes = 8 * 1024 * 1024;
constexpr qsizetype MaximumFailureExcerptBytes = 16 * 1024;

template<typename T>
shared::common::Result<T> failure(const QString& message, const QString& details, const QString& operation)
{
    return shared::common::Result<T>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Process,
        message, details, operation));
}

QString resolvedExecutable(const QString& candidate)
{
    const QString trimmed = candidate.trimmed();
    if (trimmed.isEmpty()) return {};
    QFileInfo info(trimmed);
    if (info.isAbsolute() || trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\')))
        return info.isFile() ? info.canonicalFilePath() : QString{};
    return QStandardPaths::findExecutable(trimmed);
}

bool safeMakeTarget(const QString& target)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^[A-Za-z0-9_./:-]{1,200}$)"));
    return pattern.match(target).hasMatch() && !target.startsWith(QLatin1Char('-'))
        && !target.contains(QStringLiteral(".."));
}

QString shortStatus(RunStatus status)
{
    return runStatusName(status);
}

} // namespace

BuildExecutionCoordinator::BuildExecutionCoordinator(IBuildHistoryRepository& history,
                                                       services::IProcessLauncher& processLauncher,
                                                       workspaces::WorkspaceContext& workspaceContext,
                                                       QString logDirectory,
                                                       const shared::security::RedactionService& redactor,
                                                       QObject* parent)
    : QObject(parent), m_history(history), m_processLauncher(processLauncher),
      m_workspaceContext(workspaceContext), m_logDirectory(std::move(logDirectory)), m_redactor(redactor)
{
    qRegisterMetaType<BuildRun>();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!running()) return;
        m_timedOut = true;
        emit phaseChanged(tr("Timeout reached; stopping the process tree…"));
        if (m_process) m_process->stop(2000);
    });
    connect(&m_workspaceContext, &workspaces::WorkspaceContext::workspaceChanged, this,
            [this](const QString& workspaceId, const QString&) {
        if (running() && workspaceId != m_current.workspaceId) cancel();
    });
}

bool BuildExecutionCoordinator::pathWithinWorkspace(const QString& path, const QString& workspaceRoot,
                                                     bool mustExist, QString* resolved) const
{
    QFileInfo info(path);
    const QString absolute = QDir::cleanPath(info.isAbsolute() ? path : QDir(workspaceRoot).filePath(path));
    if (mustExist && !info.exists()) return false;
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        if (mustExist) return false;
        QFileInfo ancestor(absolute);
        QString suffix;
        while (!ancestor.exists()) {
            const QString component = ancestor.fileName();
            if (!component.isEmpty()) suffix = component + QDir::separator() + suffix;
            const QString parent = ancestor.dir().absolutePath();
            if (parent == ancestor.absoluteFilePath()) return false;
            ancestor.setFile(parent);
        }
        const QString canonicalAncestor = ancestor.canonicalFilePath();
        if (canonicalAncestor.isEmpty()) return false;
        canonical = suffix.isEmpty() ? canonicalAncestor
            : QDir::cleanPath(QDir(canonicalAncestor).filePath(suffix));
    }
    const QString rootCanonical = QFileInfo(workspaceRoot).canonicalFilePath();
    if (rootCanonical.isEmpty()) return false;
    const QString root = QDir::cleanPath(rootCanonical);
    const QString candidate = QDir::cleanPath(canonical);
    const QString prefix = root.endsWith(QDir::separator()) ? root : root + QDir::separator();
#ifdef Q_OS_WIN
    const bool inside = candidate.compare(root, Qt::CaseInsensitive) == 0
        || candidate.startsWith(prefix, Qt::CaseInsensitive);
#else
    const bool inside = candidate == root || candidate.startsWith(prefix);
#endif
    if (inside && resolved) *resolved = candidate;
    return inside;
}

shared::common::Result<CommandInvocation> BuildExecutionCoordinator::resolve(const BuildProfile& profile,
                                                                               const QString& workspaceRoot,
                                                                               bool rebuild) const
{
    const auto active = m_workspaceContext.activeWorkspace();
    const QString root = QFileInfo(workspaceRoot).canonicalFilePath();
    if (!active || profile.workspaceId != active->id().toString()
        || !application::workspaces::WorkspaceValidator::pathsEqual(active->path().canonicalPath(), root))
        return failure<CommandInvocation>(tr("The selected profile does not belong to the active workspace."),
                                           profile.workspaceId, QStringLiteral("validate build profile"));
    if (running()) return failure<CommandInvocation>(tr("A build or test operation is already running."), {}, QStringLiteral("start build operation"));
    if (profile.name.trimmed().isEmpty() || profile.timeoutSeconds < 1 || profile.timeoutSeconds > 86400)
        return failure<CommandInvocation>(tr("The selected profile is invalid."), profile.name, QStringLiteral("validate build profile"));

    QString workingDirectory;
    const QString working = profile.workingDirectory.isEmpty() ? root : profile.workingDirectory;
    if (!pathWithinWorkspace(working, root, true, &workingDirectory) || !QFileInfo(workingDirectory).isDir())
        return failure<CommandInvocation>(tr("The profile working directory is missing or outside the workspace."),
                                           working, QStringLiteral("validate build working directory"));
    CommandInvocation invocation;
    invocation.workingDirectory = workingDirectory;
    const auto requireBuildDirectory = [&]() -> bool {
        QString directory;
        if (!pathWithinWorkspace(profile.buildDirectory, root, true, &directory) || !QFileInfo(directory).isDir()) return false;
        invocation.workingDirectory = root;
        return true;
    };
    QString tool;
    switch (profile.runner) {
    case RunnerType::CMakeConfigure:
        tool = resolvedExecutable(QStringLiteral("cmake"));
        if (tool.isEmpty()) return failure<CommandInvocation>(tr("CMake is not installed or could not be found on PATH."), {}, QStringLiteral("resolve CMake"));
        invocation.executable = tool;
        if (!profile.preset.isEmpty()) {
            QString directory;
            if (profile.buildDirectory.isEmpty() || !pathWithinWorkspace(profile.buildDirectory, root, false, &directory))
                return failure<CommandInvocation>(tr("The configure preset's build directory could not be verified inside the workspace."),
                    profile.buildDirectory, QStringLiteral("validate CMake configure preset directory"));
            invocation.arguments = {QStringLiteral("--preset"), profile.preset};
        }
        else {
            QString directory;
            if (profile.buildDirectory.isEmpty() || !pathWithinWorkspace(profile.buildDirectory, root, false, &directory))
                return failure<CommandInvocation>(tr("The configured build directory is outside the workspace."), profile.buildDirectory,
                                                   QStringLiteral("validate CMake build directory"));
            invocation.arguments = {QStringLiteral("-S"), root, QStringLiteral("-B"), directory};
        }
        invocation.workingDirectory = root;
        invocation.stage = tr("Configure");
        break;
    case RunnerType::CMakeBuild: {
        tool = resolvedExecutable(QStringLiteral("cmake"));
        if (tool.isEmpty()) return failure<CommandInvocation>(tr("CMake is not installed or could not be found on PATH."), {}, QStringLiteral("resolve CMake"));
        invocation.executable = tool;
        if (!profile.preset.isEmpty()) {
            QString directory;
            if (profile.buildDirectory.isEmpty() || !pathWithinWorkspace(profile.buildDirectory, root, true, &directory)
                || !QFileInfo(directory).isDir())
                return failure<CommandInvocation>(tr("The build preset's configured build directory is missing or outside the workspace."),
                    profile.buildDirectory, QStringLiteral("validate CMake build preset directory"));
            invocation.arguments = {QStringLiteral("--build"), QStringLiteral("--preset"), profile.preset};
            if (rebuild) invocation.arguments.append(QStringLiteral("--clean-first"));
        } else {
            if (!requireBuildDirectory()) return failure<CommandInvocation>(tr("Configure the CMake project before building."),
                profile.buildDirectory, QStringLiteral("validate CMake build tree"));
            invocation.arguments = {QStringLiteral("--build"), profile.buildDirectory};
            if (!profile.configuration.isEmpty()) invocation.arguments << QStringLiteral("--config") << profile.configuration;
            if (rebuild) invocation.arguments.append(QStringLiteral("--clean-first"));
        }
        invocation.stage = rebuild ? tr("Rebuild") : tr("Build");
        break;
    }
    case RunnerType::CTest: {
        tool = resolvedExecutable(QStringLiteral("ctest"));
        if (tool.isEmpty()) return failure<CommandInvocation>(tr("CTest is not installed or could not be found on PATH."), {}, QStringLiteral("resolve CTest"));
        if (profile.preset.isEmpty()) {
            if (!requireBuildDirectory()) return failure<CommandInvocation>(tr("Configure and build the CMake project before running CTest."),
                profile.buildDirectory, QStringLiteral("validate CTest build tree"));
        } else {
            QString directory;
            if (profile.buildDirectory.isEmpty() || !pathWithinWorkspace(profile.buildDirectory, root, true, &directory)
                || !QFileInfo(directory).isDir())
                return failure<CommandInvocation>(tr("The test preset's configured build directory is missing or outside the workspace."),
                    profile.buildDirectory, QStringLiteral("validate CTest preset directory"));
        }
        invocation.executable = tool;
        if (!profile.preset.isEmpty()) invocation.arguments = {QStringLiteral("--preset"), profile.preset};
        else {
            invocation.arguments = {QStringLiteral("--test-dir"), profile.buildDirectory};
            if (!profile.configuration.isEmpty()) invocation.arguments << QStringLiteral("-C") << profile.configuration;
        }
        invocation.arguments.append(QStringLiteral("--output-on-failure"));
        if (!profile.testFilter.trimmed().isEmpty()) invocation.arguments << QStringLiteral("-R") << profile.testFilter.trimmed();
        invocation.reportPath = QDir(m_logDirectory).filePath(QStringLiteral("buildtest-reports/%1.xml").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        invocation.arguments << QStringLiteral("--output-junit") << invocation.reportPath;
        invocation.stage = tr("CTest");
        break;
    }
    case RunnerType::GoBuild:
    case RunnerType::GoTest:
        tool = resolvedExecutable(QStringLiteral("go"));
        if (tool.isEmpty()) return failure<CommandInvocation>(tr("Go is not installed or could not be found on PATH."), {}, QStringLiteral("resolve Go"));
        invocation.executable = tool;
        if (profile.runner == RunnerType::GoBuild) invocation.arguments = {QStringLiteral("build")};
        else {
            invocation.arguments = {QStringLiteral("test"), QStringLiteral("-json")};
            if (!profile.testFilter.trimmed().isEmpty())
                invocation.arguments << QStringLiteral("-run") << profile.testFilter.trimmed();
        }
        if (profile.arguments.isEmpty()) invocation.arguments.append(QStringLiteral("./..."));
        else invocation.arguments.append(profile.arguments);
        invocation.stage = profile.runner == RunnerType::GoBuild ? tr("Go build") : tr("Go tests");
        break;
    case RunnerType::Pytest: {
        QString python = profile.program;
        if (python.isEmpty()) {
            const QString virtualEnvironmentPython = QDir(root).filePath(QStringLiteral(".venv/Scripts/python.exe"));
            python = QFileInfo::exists(virtualEnvironmentPython) ? virtualEnvironmentPython : QStringLiteral("python");
        }
        tool = resolvedExecutable(python);
        if (tool.isEmpty()) return failure<CommandInvocation>(tr("Python is not installed or the configured environment could not be found."), python, QStringLiteral("resolve Python"));
        invocation.executable = tool;
        invocation.arguments = {QStringLiteral("-m"), QStringLiteral("pytest"), QStringLiteral("--junitxml=%1").arg(
            QDir(m_logDirectory).filePath(QStringLiteral("buildtest-reports/%1.xml").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))))};
        invocation.reportPath = invocation.arguments.at(2).mid(QStringLiteral("--junitxml=").size());
        invocation.arguments.append(profile.arguments);
        invocation.stage = tr("pytest");
        break;
    }
    case RunnerType::NpmScript:
        if (profile.target.trimmed().isEmpty() || profile.target.contains(QRegularExpression(QStringLiteral(R"(\s)"))))
            return failure<CommandInvocation>(tr("Choose one exact npm script name in the profile."), profile.target, QStringLiteral("validate npm script"));
        tool = resolvedExecutable(QStringLiteral("npm"));
        if (tool.isEmpty()) return failure<CommandInvocation>(tr("npm is not installed or could not be found on PATH."), {}, QStringLiteral("resolve npm"));
        invocation.executable = tool;
        invocation.arguments = {QStringLiteral("run"), profile.target};
        invocation.arguments.append(profile.arguments);
        invocation.stage = tr("npm script");
        break;
    case RunnerType::MakeTarget:
        if (!safeMakeTarget(profile.target)) return failure<CommandInvocation>(tr("The Make target contains unsupported characters."), profile.target, QStringLiteral("validate Make target"));
        tool = resolvedExecutable(profile.program.isEmpty() ? QStringLiteral("make") : profile.program);
        if (tool.isEmpty()) return failure<CommandInvocation>(tr("Make is not installed or could not be found on PATH."), profile.program, QStringLiteral("resolve Make"));
        invocation.executable = tool; invocation.arguments = {profile.target};
        invocation.arguments.append(profile.arguments);
        invocation.stage = tr("Make target");
        break;
    case RunnerType::Custom:
        tool = resolvedExecutable(profile.program);
        if (tool.isEmpty()) return failure<CommandInvocation>(tr("The custom executable could not be found."), profile.program, QStringLiteral("resolve custom command"));
        invocation.executable = tool; invocation.arguments = profile.arguments;
        invocation.stage = tr("Custom command");
        break;
    }
    return shared::common::Result<CommandInvocation>::success(std::move(invocation));
}

shared::common::Result<QString> BuildExecutionCoordinator::start(const BuildProfile& profile,
                                                                  const QString& workspaceRoot,
                                                                  bool rebuild,
                                                                  bool trustedNow,
                                                                  QString taskId)
{
    if (!m_accepting) return failure<QString>(tr("Build & Test cannot start while DevHub is shutting down."), {}, QStringLiteral("start build operation"));
    if (running()) return failure<QString>(tr("A build or test operation is already running."), {}, QStringLiteral("start build operation"));
    if (!profile.trusted && !trustedNow)
        return failure<QString>(tr("Review and approve this profile before running it."), profile.name, QStringLiteral("approve build profile"));
    auto invocationResult = resolve(profile, workspaceRoot, rebuild);
    if (!invocationResult) return shared::common::Result<QString>::failure(invocationResult.error());
    m_invocation = invocationResult.value();
    m_stage = m_invocation.stage;
    m_current = {};
    m_current.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_current.workspaceId = profile.workspaceId; m_current.profileId = profile.id;
    m_current.profileName = profile.name; m_current.runner = profile.runner;
    m_current.testAction = profile.testAction;
    m_current.startedAtUtc = QDateTime::currentDateTimeUtc(); m_current.taskId = std::move(taskId);
    m_current.logReference = QDir(m_logDirectory).filePath(QStringLiteral("buildtest-logs/%1.log").arg(m_current.id));
    m_structuredOutput.clear(); m_failureExcerpt.clear(); m_logBytes = 0;
    m_logTruncated = false; m_structuredOutputTruncated = false; m_logWriteFailed = false;
    m_elapsed.invalidate();
    m_requestedCancellation = false; m_timedOut = false; m_processStarted = false;
    QDir().mkpath(QFileInfo(m_current.logReference).absolutePath());
    QDir logDirectory(QFileInfo(m_current.logReference).absolutePath());
    QFileInfoList oldLogs = logDirectory.entryInfoList({QStringLiteral("*.log")}, QDir::Files, QDir::Time);
    while (oldLogs.size() >= 100) {
        QFile::remove(oldLogs.takeLast().absoluteFilePath());
    }
    if (!m_invocation.reportPath.isEmpty()) {
        QDir().mkpath(QFileInfo(m_invocation.reportPath).absolutePath());
        QDir reportDirectory(QFileInfo(m_invocation.reportPath).absolutePath());
        QFileInfoList oldReports = reportDirectory.entryInfoList({QStringLiteral("*.xml")}, QDir::Files, QDir::Time);
        while (oldReports.size() >= 100) QFile::remove(oldReports.takeLast().absoluteFilePath());
    }
    m_logFile.setFileName(m_current.logReference);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_current = {};
        return failure<QString>(tr("DevHub could not create a build log."), m_logFile.errorString(), QStringLiteral("create build log"));
    }
    auto recorded = m_history.startRun(m_current);
    if (!recorded) {
        m_logFile.close(); QFile::remove(m_current.logReference); m_current = {};
        return shared::common::Result<QString>::failure(recorded.error());
    }

    domain::services::ServiceDefinition definition;
    definition.id = m_current.id; definition.workspaceId = profile.workspaceId;
    definition.name = profile.name; definition.program = m_invocation.executable;
    definition.arguments = m_invocation.arguments; definition.workingDirectory = m_invocation.workingDirectory;
    definition.shellMode = false;
    m_process = m_processLauncher.launch(definition, this);
    if (!m_process) {
        m_current.status = RunStatus::StartFailed;
        m_current.finishedAtUtc = QDateTime::currentDateTimeUtc();
        m_current.summary = tr("DevHub could not create the process.");
        m_logFile.close();
        (void)m_history.finishRun(m_current);
        const BuildRun finished = m_current; m_current = {};
        emit runStartFailed(finished.summary); emit runFinished(finished);
        return failure<QString>(finished.summary, {}, QStringLiteral("launch build process"));
    }
    connect(m_process, &services::IManagedProcess::started, this, [this, timeout=profile.timeoutSeconds] {
        m_processStarted = true; m_elapsed.start(); m_timeout.start(timeout * 1000);
        emit phaseChanged(m_stage);
    });
    connect(m_process, &services::IManagedProcess::outputAvailable, this,
            [this](const QByteArray& bytes, bool standardError) { appendOutput(bytes, standardError); });
    connect(m_process, &services::IManagedProcess::processError, this, [this](const QString& message) {
        if (!running()) return;
        const QString safeMessage = m_redactor.redact(message);
        m_current.summary = safeMessage;
        emit phaseChanged(safeMessage);
        if (!m_processStarted) QTimer::singleShot(100, this, [this] {
            if (running() && !m_processStarted) finish(-1, static_cast<int>(QProcess::CrashExit));
        });
    });
    connect(m_process, &services::IManagedProcess::finished, this,
            [this](int exitCode, int exitStatus) { finish(exitCode, exitStatus); });
    m_process->start();
    emit phaseChanged(tr("Starting %1…").arg(m_stage));
    return shared::common::Result<QString>::success(m_current.id);
}

void BuildExecutionCoordinator::appendOutput(const QByteArray& bytes, bool standardError)
{
    if (!running() || bytes.isEmpty()) return;
    QByteArray safe = m_redactor.redact(QString::fromUtf8(bytes)).toUtf8();
    if (m_logBytes < MaximumLogBytes) {
        const qsizetype allowed = qMin<qsizetype>(safe.size(), MaximumLogBytes - m_logBytes);
        if (allowed > 0) {
            const qint64 written = m_logFile.write(safe.constData(), allowed);
            if (written < 0) m_logWriteFailed = true;
            else {
                m_logBytes += written;
                if (written != allowed) m_logWriteFailed = true;
            }
        }
        if (allowed < safe.size()) m_logTruncated = true;
    } else {
        m_logTruncated = true;
    }
    if (standardError) {
        m_failureExcerpt += safe;
        if (m_failureExcerpt.size() > MaximumFailureExcerptBytes)
            m_failureExcerpt = m_failureExcerpt.right(MaximumFailureExcerptBytes);
    }
    if (m_current.runner == RunnerType::GoTest) {
        if (m_structuredOutput.size() < MaximumStructuredBytes) {
            const qsizetype allowed = qMin<qsizetype>(safe.size(), MaximumStructuredBytes - m_structuredOutput.size());
            m_structuredOutput.append(safe.constData(), allowed);
            if (allowed < safe.size()) m_structuredOutputTruncated = true;
        } else if (!safe.isEmpty()) {
            m_structuredOutputTruncated = true;
        }
    }
    emit outputAvailable(m_current.id, safe, standardError);
}

void BuildExecutionCoordinator::finish(int exitCode, int exitStatus)
{
    if (!running()) return;
    m_timeout.stop();
    if (m_elapsed.isValid()) m_current.durationMilliseconds = m_elapsed.elapsed();
    else m_current.durationMilliseconds = 0;
    m_current.finishedAtUtc = QDateTime::currentDateTimeUtc();
    m_current.exitCode = exitCode;
    if (m_logFile.isOpen()) {
        if (!m_logFile.flush()) m_logWriteFailed = true;
        m_logFile.close();
    }

    if (!m_invocation.reportPath.isEmpty()) {
        QFile report(m_invocation.reportPath);
        if (report.open(QIODevice::ReadOnly)) {
            constexpr qint64 MaximumReportBytes = 4 * 1024 * 1024;
            if (report.size() > MaximumReportBytes) m_structuredOutputTruncated = true;
            const QByteArray data = report.read(MaximumReportBytes);
            m_current.tests = BuildResultParser::parseJUnit(data);
            report.close(); QFile::remove(m_invocation.reportPath);
        }
    } else if (m_current.runner == RunnerType::GoTest) {
        m_current.tests = BuildResultParser::parseGoJson(m_structuredOutput);
    }
    BuildResultParser::summarizeTests(m_current);
    if (m_current.runner == RunnerType::CMakeBuild || m_current.runner == RunnerType::GoBuild
        || m_current.runner == RunnerType::Custom || m_current.runner == RunnerType::MakeTarget
        || m_current.runner == RunnerType::NpmScript)
        m_current.diagnostics = BuildResultParser::parseDiagnostics(QString::fromUtf8(m_failureExcerpt), m_stage);

    if (m_timedOut) m_current.status = RunStatus::TimedOut;
    else if (m_requestedCancellation) m_current.status = RunStatus::Cancelled;
    else if (!m_processStarted) m_current.status = RunStatus::StartFailed;
    else if (exitStatus == static_cast<int>(QProcess::NormalExit) && exitCode == 0) m_current.status = RunStatus::Succeeded;
    else m_current.status = RunStatus::Failed;
    if (m_current.status == RunStatus::Running) m_current.status = RunStatus::Failed;

    QString summary;
    if (isTestRunner(m_current.runner) || m_current.testAction) {
        if (m_current.totalTests > 0)
            summary = tr("%1 passed · %2 failed · %3 skipped").arg(m_current.passedTests).arg(m_current.failedTests).arg(m_current.skippedTests);
        else if (m_current.status == RunStatus::Succeeded)
            summary = tr("Test command completed; no structured test results were available.");
        else summary = tr("Test command %1; no structured results were available.").arg(shortStatus(m_current.status).toLower());
    } else if (!m_current.summary.isEmpty()) {
        summary = m_current.summary;
    } else {
        summary = m_current.status == RunStatus::Succeeded ? tr("Process completed successfully.")
            : !m_failureExcerpt.isEmpty() ? QString::fromUtf8(m_failureExcerpt).trimmed().right(1000)
            : tr("Process %1 (exit code %2).").arg(shortStatus(m_current.status)).arg(exitCode);
    }
    if (m_current.status == RunStatus::Cancelled) summary = tr("Cancelled by user or workspace change.");
    if (m_current.status == RunStatus::TimedOut) summary = tr("Timed out after %1 seconds.").arg(m_elapsed.elapsed() / 1000);
    if (m_logTruncated) summary += tr(" Execution log was truncated at 4 MiB.");
    if (m_logWriteFailed) summary += tr(" DevHub could not save all execution output.");
    if (m_structuredOutputTruncated) summary += tr(" Structured test output was truncated; test results may be incomplete.");
    m_current.summary = m_redactor.redact(summary).left(4000);
    const auto stored = m_history.finishRun(m_current);
    if (!stored && m_current.summary.size() < 3500)
        m_current.summary += tr(" History could not be saved: %1").arg(stored.error().userMessage);
    const BuildRun completed = m_current;
    if (m_process) { m_process->deleteLater(); m_process = nullptr; }
    m_current = {};
    emit phaseChanged(tr("Idle"));
    emit runFinished(completed);
}

void BuildExecutionCoordinator::cancel()
{
    if (!running() || !m_process) return;
    m_requestedCancellation = true;
    m_timeout.stop();
    emit phaseChanged(tr("Cancellation requested…"));
    m_process->stop(2000);
}

void BuildExecutionCoordinator::cancelWorkspace(const QString& workspaceId)
{
    if (running() && m_current.workspaceId == workspaceId) cancel();
}

void BuildExecutionCoordinator::beginShutdown(std::function<void()> completion)
{
    m_accepting = false;
    if (!running()) { if (completion) completion(); return; }
    const auto completed = std::make_shared<bool>(false);
    const auto connection = std::make_shared<QMetaObject::Connection>();
    const auto finishShutdown = std::make_shared<std::function<void()>>(std::move(completion));
    *connection = connect(this, &BuildExecutionCoordinator::runFinished, this,
                          [completed, connection, finishShutdown](const BuildRun&) {
        if (*completed) return;
        *completed = true;
        QObject::disconnect(*connection);
        if (*finishShutdown) (*finishShutdown)();
    });
    cancel();
    QTimer::singleShot(7000, this, [this, completed, connection, finishShutdown] {
        if (*completed) return;
        *completed = true;
        QObject::disconnect(*connection);
        if (m_process) m_process->stop(100);
        if (*finishShutdown) (*finishShutdown)();
    });
}

} // namespace devhub::application::buildtest
