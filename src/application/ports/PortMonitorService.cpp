#include "PortMonitorService.h"

#include "../../domain/errors/AppError.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace devhub::application::ports {
namespace {
using devhub::domain::errors::AppError;
using devhub::domain::errors::ErrorCategory;
using devhub::domain::errors::ErrorCode;

AppError processError(const QString& details, const QString& operation)
{
    return AppError::failure(ErrorCode::IoFailure, ErrorCategory::Port,
        QStringLiteral("DevHub could not complete the process action."), details, operation);
}
}

QString protocolName(PortProtocol protocol)
{
    return protocol == PortProtocol::Tcp ? QStringLiteral("TCP") : QStringLiteral("UDP");
}

QString tcpStateName(TcpState state)
{
    switch (state) {
    case TcpState::Closed: return QStringLiteral("Closed");
    case TcpState::Listen: return QStringLiteral("Listening");
    case TcpState::SynSent: return QStringLiteral("SYN sent");
    case TcpState::SynReceived: return QStringLiteral("SYN received");
    case TcpState::Established: return QStringLiteral("Established");
    case TcpState::FinWait1: return QStringLiteral("FIN wait 1");
    case TcpState::FinWait2: return QStringLiteral("FIN wait 2");
    case TcpState::CloseWait: return QStringLiteral("Close wait");
    case TcpState::Closing: return QStringLiteral("Closing");
    case TcpState::LastAck: return QStringLiteral("Last ACK");
    case TcpState::TimeWait: return QStringLiteral("Time wait");
    case TcpState::DeleteTcb: return QStringLiteral("Delete TCB");
    case TcpState::Unknown: return QStringLiteral("—");
    }
    return QStringLiteral("—");
}

PortMonitorService::PortMonitorService(IPortProvider& provider, IProcessInspector& inspector, QObject* parent)
    : QObject(parent), m_provider(provider), m_inspector(inspector)
{
    qRegisterMetaType<PortRecord>();
    connect(&m_watcher, &QFutureWatcher<RefreshResult>::finished, this, [this] {
        const RefreshResult result = m_watcher.result();
        if (!result.error.isEmpty()) {
            emit refreshFailed(result.error);
            return;
        }
        m_records = result.records;
        emit recordsChanged(m_records);
        emit refreshFinished(static_cast<int>(m_records.size()));
    });
}

PortMonitorService::~PortMonitorService()
{
    // QFutureWatcher destruction does not wait for QtConcurrent::run. The
    // provider and inspector must remain alive until their job has returned.
    m_watcher.waitForFinished();
}

bool PortMonitorService::refresh()
{
    if (m_watcher.isRunning()) return false;
    emit refreshStarted();
    IPortProvider* provider = &m_provider;
    IProcessInspector* inspector = &m_inspector;
    m_watcher.setFuture(QtConcurrent::run([provider, inspector] {
        RefreshResult result;
        auto table = provider->enumerate();
        if (!table) {
            result.error = table.error().userMessage;
            return result;
        }
        result.records = std::move(table).value();
        QHash<quint32, ProcessDetails> resolved;
        QSet<quint32> inspected;
        for (PortRecord& record : result.records) {
            const quint32 pid = record.process.processId;
            if (pid == 0) {
                record.processName = QStringLiteral("System idle");
                continue;
            }
            if (!inspected.contains(pid)) {
                inspected.insert(pid);
                auto details = inspector->inspect(pid);
                if (details) resolved.insert(pid, std::move(details).value());
                else record.processError = details.error().userMessage;
            }
            if (resolved.contains(pid)) {
                const ProcessDetails details = resolved.value(pid);
                record.process = details.identity;
                record.processName = details.processName;
                record.executablePath = details.executablePath;
            } else if (record.processError.isEmpty()) {
                record.processError = QStringLiteral("Process is no longer available.");
            }
        }
        return result;
    }));
    return true;
}

shared::common::Result<void> PortMonitorService::terminate(const PortRecord& record)
{
#ifdef Q_OS_WIN
    const quint32 pid = record.process.processId;
    if (pid <= 4 || pid == GetCurrentProcessId()) {
        return shared::common::Result<void>::failure(processError(
            QStringLiteral("Refused to terminate a protected system or current process."), QStringLiteral("terminate process")));
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    if (!process) {
        return shared::common::Result<void>::failure(processError(
            QStringLiteral("OpenProcess failed with Windows error %1.").arg(GetLastError()), QStringLiteral("terminate process")));
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool hasIdentity = GetProcessTimes(process, &created, &exited, &kernel, &user) != 0;
    const quint64 creation = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    if (!hasIdentity || creation != record.process.creationTime) {
        CloseHandle(process);
        return shared::common::Result<void>::failure(processError(
            QStringLiteral("The process changed since the port list was refreshed. Refresh and try again."),
            QStringLiteral("revalidate process before termination")));
    }
    if (!TerminateProcess(process, 1)) {
        const DWORD error = GetLastError();
        CloseHandle(process);
        return shared::common::Result<void>::failure(processError(
            QStringLiteral("Windows denied process termination (error %1). No elevation was attempted.").arg(error),
            QStringLiteral("terminate process")));
    }
    CloseHandle(process);
    return shared::common::Result<void>::success();
#else
    Q_UNUSED(record);
    return shared::common::Result<void>::failure(processError(QStringLiteral("Process termination is only available on Windows."),
                                                               QStringLiteral("terminate process")));
#endif
}

} // namespace devhub::application::ports
