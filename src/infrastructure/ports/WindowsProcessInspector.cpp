#include "WindowsProcessInspector.h"

#include "../../domain/errors/AppError.h"

#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace devhub::infrastructure::ports {

shared::common::Result<application::ports::ProcessDetails> WindowsProcessInspector::inspect(quint32 processId)
{
#ifdef Q_OS_WIN
    using namespace application::ports;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        const DWORD error = GetLastError();
        return shared::common::Result<ProcessDetails>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Port,
            error == ERROR_ACCESS_DENIED ? QStringLiteral("Access denied") : QStringLiteral("Process is no longer available"),
            QStringLiteral("OpenProcess(%1) failed with Windows error %2.").arg(processId).arg(error),
            QStringLiteral("inspect port owner")));
    }
    wchar_t path[32768]{};
    DWORD pathLength = static_cast<DWORD>(std::size(path));
    const bool pathAvailable = QueryFullProcessImageNameW(process, 0, path, &pathLength) != 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool timeAvailable = GetProcessTimes(process, &created, &exited, &kernel, &user) != 0;
    const DWORD error = GetLastError();
    CloseHandle(process);
    if (!timeAvailable) {
        return shared::common::Result<ProcessDetails>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Port,
            QStringLiteral("Process is no longer available"),
            QStringLiteral("GetProcessTimes(%1) failed with Windows error %2.").arg(processId).arg(error),
            QStringLiteral("inspect port owner")));
    }
    const quint64 creation = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    const QString executable = pathAvailable ? QString::fromWCharArray(path, static_cast<qsizetype>(pathLength)) : QString();
    ProcessDetails result;
    result.identity = {processId, creation};
    result.executablePath = executable;
    result.processName = executable.isEmpty() ? QStringLiteral("PID %1").arg(processId) : QFileInfo(executable).fileName();
    return shared::common::Result<ProcessDetails>::success(std::move(result));
#else
    Q_UNUSED(processId);
    return shared::common::Result<application::ports::ProcessDetails>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Port,
        QStringLiteral("Process inspection is only available on Windows."), QStringLiteral("Non-Windows platform."),
        QStringLiteral("inspect port owner")));
#endif
}

} // namespace devhub::infrastructure::ports
