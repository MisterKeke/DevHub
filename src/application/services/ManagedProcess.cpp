#include "ManagedProcess.h"
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <TlHelp32.h>
#endif

namespace devhub::application::services {
namespace {
QString shellCommand(const QStringList& parts) {
    QStringList quoted;
    for (QString part : parts) {
        part.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        if (part.contains(QRegularExpression(QStringLiteral("\\s")))) part = QLatin1Char('"') + part + QLatin1Char('"');
        quoted.append(std::move(part));
    }
    return quoted.join(QLatin1Char(' '));
}
#ifdef Q_OS_WIN
bool resumeInitialThread(DWORD processId, DWORD* errorCode)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        if (errorCode) *errorCode = GetLastError();
        return false;
    }
    THREADENTRY32 entry{};
    entry.dwSize = static_cast<DWORD>(sizeof(entry));
    bool resumed = false;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != processId) continue;
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
            if (!thread) {
                if (errorCode) *errorCode = GetLastError();
                continue;
            }
            if (ResumeThread(thread) != static_cast<DWORD>(-1)) resumed = true;
            else if (errorCode) *errorCode = GetLastError();
            CloseHandle(thread);
            if (resumed) break;
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (!resumed && errorCode && *errorCode == ERROR_SUCCESS) *errorCode = ERROR_NOT_FOUND;
    return resumed;
}
#endif
}
QProcessManagedProcess::QProcessManagedProcess(domain::services::ServiceDefinition definition, QObject* parent)
    : IManagedProcess(parent), m_definition(std::move(definition)) {
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { emit outputAvailable(m_process.readAllStandardOutput(), false); });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { emit outputAvailable(m_process.readAllStandardError(), true); });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            finishOwnedProcessTree();
            emit finished(code, static_cast<int>(status));
        });
    connect(&m_process, &QProcess::started, this, [this] {
#ifdef Q_OS_WIN
        const DWORD processId = static_cast<DWORD>(m_process.processId());
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, processId);
        if (!process || !m_jobObject || !AssignProcessToJobObject(static_cast<HANDLE>(m_jobObject), process)) {
            const DWORD error = GetLastError();
            if (process) CloseHandle(process);
            emit processError(QStringLiteral("Could not take ownership of the service process tree (Windows error %1).")
                                  .arg(error));
            m_process.kill();
            finishOwnedProcessTree();
            return;
        }
        CloseHandle(process);
        DWORD resumeError = ERROR_SUCCESS;
        if (!resumeInitialThread(processId, &resumeError)) {
            emit processError(QStringLiteral("Could not resume the owned service process (Windows error %1).")
                                  .arg(resumeError));
            TerminateJobObject(static_cast<HANDLE>(m_jobObject), 1);
            finishOwnedProcessTree();
            m_process.kill();
            return;
        }
#endif
        emit started();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        emit processError(QStringLiteral("QProcess error %1: %2").arg(static_cast<int>(error)).arg(m_process.errorString()));
    });
}
QProcessManagedProcess::~QProcessManagedProcess() { finishOwnedProcessTree(); }
void QProcessManagedProcess::start() {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(m_definition.workingDirectory);
    if (!m_definition.shellMode) {
        m_process.setProgram(m_definition.program); m_process.setArguments(m_definition.arguments);
    } else {
#ifdef Q_OS_WIN
        m_process.setProgram(QStringLiteral("cmd.exe"));
        m_process.setArguments({QStringLiteral("/S"), QStringLiteral("/C"), shellCommand(QStringList{m_definition.program} + m_definition.arguments)});
#else
        m_process.setProgram(QStringLiteral("/bin/sh"));
        m_process.setArguments({QStringLiteral("-c"), shellCommand(QStringList{m_definition.program} + m_definition.arguments)});
#endif
    }
#ifdef Q_OS_WIN
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        emit processError(QStringLiteral("Could not create an owned service process group (Windows error %1).")
                              .arg(GetLastError()));
        return;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        const DWORD error = GetLastError(); CloseHandle(job);
        emit processError(QStringLiteral("Could not configure service process cleanup (Windows error %1).").arg(error));
        return;
    }
    m_jobObject = job;
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_SUSPENDED;
    });
#endif
    m_process.start();
}
void QProcessManagedProcess::stop(int timeoutMs) {
    if (m_process.state() == QProcess::NotRunning) return;
    m_process.terminate();
    QTimer::singleShot(qBound(100, timeoutMs, 120000), this, [this] {
        if (m_process.state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
            if (m_jobObject) TerminateJobObject(static_cast<HANDLE>(m_jobObject), 1);
#endif
            m_process.kill();
        }
    });
}
qint64 QProcessManagedProcess::processId() const { return static_cast<qint64>(m_process.processId()); }
void QProcessManagedProcess::finishOwnedProcessTree()
{
#ifdef Q_OS_WIN
    if (m_jobObject) {
        const HANDLE job = static_cast<HANDLE>(m_jobObject);
        m_jobObject = nullptr;
        CloseHandle(job); // KILL_ON_JOB_CLOSE stops surviving descendants after the root exits.
    }
#endif
}
IManagedProcess* QProcessLauncher::launch(const domain::services::ServiceDefinition& definition, QObject* parent) {
    return new QProcessManagedProcess(definition, parent);
}
}
