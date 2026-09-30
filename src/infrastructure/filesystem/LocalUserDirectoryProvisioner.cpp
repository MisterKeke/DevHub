#include "LocalUserDirectoryProvisioner.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <utility>

namespace devhub::infrastructure::filesystem {
namespace {

shared::common::Result<void> directoryFailure(const QString& technicalDetails)
{
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::UserDirectoryInitializationFailed,
        domain::errors::ErrorCategory::FileSystem,
        QStringLiteral("DevHub could not prepare the account's local folders."),
        technicalDetails,
        QStringLiteral("prepare account folders")));
}

} // namespace

LocalUserDirectoryProvisioner::LocalUserDirectoryProvisioner(QString usersDirectory)
    : m_usersDirectory(QDir(std::move(usersDirectory)).absolutePath())
{
}

QString LocalUserDirectoryProvisioner::accountPath(const domain::account::AccountId& accountId) const
{
    return QDir(m_usersDirectory).filePath(accountId.toString());
}

shared::common::Result<void> LocalUserDirectoryProvisioner::createInitialDirectories(
    const domain::account::AccountId& accountId)
{
    QDir root(m_usersDirectory);
    if (!root.exists() && !QDir().mkpath(m_usersDirectory)) {
        return directoryFailure(QStringLiteral("The users root directory could not be created."));
    }
    const QString targetPath = accountPath(accountId);
    if (QFileInfo::exists(targetPath)) {
        return directoryFailure(QStringLiteral("An account directory with the generated identifier already exists."));
    }

    QTemporaryDir staged(QDir(m_usersDirectory).filePath(QStringLiteral(".new-user-XXXXXX")));
    if (!staged.isValid()) {
        return directoryFailure(QStringLiteral("A staging directory for the account could not be created."));
    }
    for (const QString& child : {QStringLiteral("notes"), QStringLiteral("recovery"), QStringLiteral("cache")}) {
        if (!QDir().mkpath(QDir(staged.path()).filePath(child))) {
            return directoryFailure(QStringLiteral("The required account directory structure could not be created."));
        }
    }
    if (!QDir(m_usersDirectory).rename(staged.path(), targetPath)) {
        return directoryFailure(QStringLiteral("The prepared account directory could not be committed."));
    }
    return shared::common::Result<void>::success();
}

shared::common::Result<void> LocalUserDirectoryProvisioner::removeUserDirectory(
    const domain::account::AccountId& accountId)
{
    const QString targetPath = accountPath(accountId);
    const QFileInfo targetInfo(targetPath);
    const QFileInfo usersInfo(m_usersDirectory);
    if (targetInfo.isSymLink() || !targetInfo.isDir()
        || targetInfo.absolutePath() != usersInfo.absoluteFilePath()) {
        return directoryFailure(QStringLiteral("The account folder did not pass containment validation."));
    }
    if (!QDir(targetPath).removeRecursively()) {
        return directoryFailure(QStringLiteral("The account folder could not be rolled back."));
    }
    return shared::common::Result<void>::success();
}

} // namespace devhub::infrastructure::filesystem
