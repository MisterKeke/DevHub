#include "WindowsTrashService.h"

#include <QFileInfo>
#include <QDir>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <Shellapi.h>
#endif

namespace devhub::infrastructure::files {

shared::common::Result<void> WindowsTrashService::moveToTrash(const QString& path)
{
#ifdef Q_OS_WIN
    if (!QFileInfo::exists(path)) return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::FileSystem,
        QStringLiteral("This file or folder no longer exists."), path, QStringLiteral("move file to Recycle Bin")));
    QString from = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    from.append(QChar(0)); from.append(QChar(0));
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = reinterpret_cast<LPCWSTR>(from.utf16());
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    const int status = SHFileOperationW(&operation);
    if (status != 0 || operation.fAnyOperationsAborted)
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::FileSystem,
            QStringLiteral("Windows could not move this item to the Recycle Bin."), QStringLiteral("SHFileOperationW returned %1").arg(status),
            QStringLiteral("move file to Recycle Bin")));
    return shared::common::Result<void>::success();
#else
    Q_UNUSED(path);
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::FileSystem,
        QStringLiteral("Recycle Bin is unavailable in this build."), QStringLiteral("Windows shell trash API unavailable."),
        QStringLiteral("move file to Recycle Bin")));
#endif
}

} // namespace devhub::infrastructure::files
