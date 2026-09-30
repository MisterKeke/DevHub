#include "WorkspaceValidation.h"

#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace devhub::application::workspaces {
namespace {
shared::common::Result<ValidatedWorkspacePath> invalidPath(const QString& text, const QString& reason)
{
    return shared::common::Result<ValidatedWorkspacePath>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::PathUnavailable, domain::errors::ErrorCategory::Workspace,
        text, reason, QStringLiteral("validate workspace path")));
}
}

shared::common::Result<ValidatedWorkspacePath> WorkspaceValidator::validate(const QString& enteredPath) const
{
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(enteredPath.trimmed()));
    if (cleaned.isEmpty() || cleaned == QStringLiteral("."))
        return invalidPath(QStringLiteral("Choose a workspace folder."), QStringLiteral("The selected path is empty."));
    const QFileInfo info(cleaned);
    if (info.exists() && !info.isDir())
        return invalidPath(QStringLiteral("Choose a folder, not a file."), QStringLiteral("Workspace path is not a directory: %1").arg(cleaned));
    const auto inspected = inspect(cleaned);
    if (inspected == domain::workspace::WorkspaceStatus::Missing)
        return invalidPath(QStringLiteral("That folder cannot be found."), QStringLiteral("Workspace folder is missing: %1").arg(cleaned));
    if (inspected == domain::workspace::WorkspaceStatus::Inaccessible)
        return invalidPath(QStringLiteral("DevHub cannot read that folder."), QStringLiteral("Workspace folder is inaccessible: %1").arg(cleaned));
    if (!info.isReadable())
        return invalidPath(QStringLiteral("DevHub cannot read that folder."), QStringLiteral("Workspace folder is inaccessible: %1").arg(cleaned));
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        return invalidPath(QStringLiteral("DevHub cannot access that folder."), QStringLiteral("Unable to canonicalize workspace folder: %1").arg(cleaned));
    QString canonicalPath = QDir::cleanPath(QDir::fromNativeSeparators(canonical));
#ifdef Q_OS_WIN
    canonicalPath = canonicalPath.toCaseFolded();
#endif
    return shared::common::Result<ValidatedWorkspacePath>::success({
        domain::workspace::WorkspacePath(info.absoluteFilePath(), canonicalPath),
        domain::workspace::WorkspaceStatus::Ready});
}

domain::workspace::WorkspaceStatus WorkspaceValidator::inspect(const QString& path) const
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED || error == ERROR_BAD_NETPATH || error == ERROR_BAD_NET_NAME
            || error == ERROR_NETWORK_UNREACHABLE || error == ERROR_NOT_READY)
            return domain::workspace::WorkspaceStatus::Inaccessible;
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_DRIVE)
            return domain::workspace::WorkspaceStatus::Missing;
        return domain::workspace::WorkspaceStatus::Inaccessible;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return domain::workspace::WorkspaceStatus::Inaccessible;
#endif
    const QFileInfo info(path);
#ifndef Q_OS_WIN
    if (!info.exists()) return domain::workspace::WorkspaceStatus::Missing;
#endif
    if (!info.isDir() || !info.isReadable()) return domain::workspace::WorkspaceStatus::Inaccessible;
    QDir directory(path);
    if (!directory.exists() || !directory.isReadable()) return domain::workspace::WorkspaceStatus::Inaccessible;
    return domain::workspace::WorkspaceStatus::Ready;
}

bool WorkspaceValidator::pathsEqual(const QString& left, const QString& right)
{
    const QString a = QDir::cleanPath(QDir::fromNativeSeparators(left));
    const QString b = QDir::cleanPath(QDir::fromNativeSeparators(right));
#ifdef Q_OS_WIN
    return QString::compare(a, b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

} // namespace devhub::application::workspaces
