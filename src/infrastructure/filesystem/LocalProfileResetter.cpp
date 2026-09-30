#include "LocalProfileResetter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace devhub::infrastructure::filesystem {
namespace {

shared::common::Result<void> resetFailure(const QString& details)
{
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::ProfileResetFailed,
        domain::errors::ErrorCategory::FileSystem,
        QStringLiteral("DevHub could not reset the local profile."),
        details,
        QStringLiteral("reset local profile")));
}

bool moveIfPresent(const QString& from, const QString& to)
{
    if (!QFileInfo::exists(from)) {
        return true;
    }
    return QFile::rename(from, to);
}

void removeIfPresent(const QString& path)
{
    if (QFileInfo::exists(path)) {
        QFile::remove(path);
    }
}

} // namespace

LocalProfileResetter::LocalProfileResetter(QString localDataDirectory,
                                           QString usersDirectory,
                                           database::InternalDatabaseManager& database)
    : m_localDataDirectory(QDir(std::move(localDataDirectory)).absolutePath()),
      m_usersDirectory(QDir(std::move(usersDirectory)).absolutePath()),
      m_database(database)
{
}

shared::common::Result<void> LocalProfileResetter::resetProfile(const domain::account::AccountId& accountId)
{
    const QString recoveryRoot = QDir(m_localDataDirectory).filePath(QStringLiteral("recovery"));
    if (!QDir().mkpath(recoveryRoot)) {
        return resetFailure(QStringLiteral("The recovery directory could not be created."));
    }
    const QString recoveryPath = QDir(recoveryRoot).filePath(
        QStringLiteral("profile-%1-%2")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'")),
                 QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(recoveryPath)) {
        return resetFailure(QStringLiteral("A unique recovery destination could not be created."));
    }

    auto closed = m_database.close();
    if (!closed) {
        QDir(recoveryPath).removeRecursively();
        return shared::common::Result<void>::failure(closed.error());
    }
    const QString databaseFile = m_database.databaseFilePath();
    const QString archivedDatabase = QDir(recoveryPath).filePath(QStringLiteral("devhub.db"));
    const QString archivedWal = QDir(recoveryPath).filePath(QStringLiteral("devhub.db-wal"));
    const QString archivedShm = QDir(recoveryPath).filePath(QStringLiteral("devhub.db-shm"));
    const QString archivedUserDirectory = QDir(recoveryPath).filePath(
        QStringLiteral("user-%1").arg(accountId.toString()));
    const QString userDirectory = QDir(m_usersDirectory).filePath(accountId.toString());
    bool userDirectoryMoved = false;
    bool databaseMoved = false;
    bool walMoved = false;
    bool shmMoved = false;

    if (QFileInfo::exists(userDirectory)) {
        userDirectoryMoved = QDir(m_usersDirectory).rename(userDirectory, archivedUserDirectory);
        if (!userDirectoryMoved) {
            auto reopened = m_database.open();
            QDir(recoveryPath).removeRecursively();
            if (!reopened) {
                return resetFailure(QStringLiteral("The old database could not be reopened after the directory move failed."));
            }
            return resetFailure(QStringLiteral("The local user directory could not be preserved."));
        }
    }
    databaseMoved = QFile::rename(databaseFile, archivedDatabase);
    if (databaseMoved) {
        walMoved = moveIfPresent(databaseFile + QStringLiteral("-wal"), archivedWal);
        shmMoved = moveIfPresent(databaseFile + QStringLiteral("-shm"), archivedShm);
    }
    if (!databaseMoved || !walMoved || !shmMoved) {
        if (userDirectoryMoved) {
            QDir(recoveryPath).rename(archivedUserDirectory, userDirectory);
        }
        if (databaseMoved) {
            QFile::rename(archivedDatabase, databaseFile);
        }
        if (walMoved) {
            QFile::rename(archivedWal, databaseFile + QStringLiteral("-wal"));
        }
        if (shmMoved) {
            QFile::rename(archivedShm, databaseFile + QStringLiteral("-shm"));
        }
        auto reopened = m_database.open();
        QDir(recoveryPath).removeRecursively();
        if (!reopened) {
            return resetFailure(QStringLiteral("The old database could not be reopened after profile preservation failed."));
        }
        return resetFailure(QStringLiteral("The local profile database could not be preserved."));
    }

    auto opened = m_database.open();
    auto migrated = opened ? m_database.migrate() : shared::common::Result<void>::failure(opened.error());
    if (!migrated) {
        auto closedAgain = m_database.close();
        if (!closedAgain) {
            return resetFailure(QStringLiteral("The new database could not be closed while restoring the old profile."));
        }
        removeIfPresent(databaseFile);
        removeIfPresent(databaseFile + QStringLiteral("-wal"));
        removeIfPresent(databaseFile + QStringLiteral("-shm"));
        const bool dbRestored = QFile::rename(archivedDatabase, databaseFile);
        const bool walRestored = !walMoved || QFile::rename(archivedWal, databaseFile + QStringLiteral("-wal"));
        const bool shmRestored = !shmMoved || QFile::rename(archivedShm, databaseFile + QStringLiteral("-shm"));
        const bool userRestored = !userDirectoryMoved
            || QDir(recoveryPath).rename(archivedUserDirectory, userDirectory);
        auto reopened = m_database.open();
        if (!dbRestored || !walRestored || !shmRestored || !userRestored) {
            return resetFailure(QStringLiteral("The new database failed to initialize and profile rollback was incomplete."));
        }
        if (!reopened) {
            return resetFailure(QStringLiteral("The old profile was restored but its database could not be reopened."));
        }
        QDir(recoveryPath).removeRecursively();
        return shared::common::Result<void>::failure(migrated.error());
    }

    return shared::common::Result<void>::success();
}

} // namespace devhub::infrastructure::filesystem
