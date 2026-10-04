#include "LocalProfileResetter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <utility>

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

bool removeIfPresent(const QString& path)
{
    return !QFileInfo::exists(path) || QFile::remove(path);
}

} // namespace

LocalProfileResetter::LocalProfileResetter(QString localDataDirectory,
                                           QString usersDirectory,
                                           database::InternalDatabaseManager& database,
                                           MovePath movePath)
    : m_localDataDirectory(QDir(std::move(localDataDirectory)).absolutePath()),
      m_usersDirectory(QDir(std::move(usersDirectory)).absolutePath()),
      m_database(database),
      m_movePath(movePath ? std::move(movePath) : MovePath([](const QString& from, const QString& to) {
          return QFileInfo(from).isDir() ? QDir().rename(from, to) : QFile::rename(from, to);
      }))
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
        userDirectoryMoved = m_movePath(userDirectory, archivedUserDirectory);
        if (!userDirectoryMoved) {
            auto reopened = m_database.open();
            QDir(recoveryPath).removeRecursively();
            if (!reopened) {
                return resetFailure(QStringLiteral("The old database could not be reopened after the directory move failed."));
            }
            return resetFailure(QStringLiteral("The local user directory could not be preserved."));
        }
    }
    databaseMoved = m_movePath(databaseFile, archivedDatabase);
    bool preserved = databaseMoved;
    if (databaseMoved) {
        if (QFileInfo::exists(databaseFile + QStringLiteral("-wal"))) {
            walMoved = m_movePath(databaseFile + QStringLiteral("-wal"), archivedWal);
            preserved = preserved && walMoved;
        }
        if (QFileInfo::exists(databaseFile + QStringLiteral("-shm"))) {
            shmMoved = m_movePath(databaseFile + QStringLiteral("-shm"), archivedShm);
            preserved = preserved && shmMoved;
        }
    }
    const auto restoreProfile = [&] {
        // Attempt every restoration, retaining anything that could not be
        // moved back. Never open/recreate the database after a partial rollback.
        const bool shmRestored = !shmMoved || m_movePath(archivedShm, databaseFile + QStringLiteral("-shm"));
        const bool walRestored = !walMoved || m_movePath(archivedWal, databaseFile + QStringLiteral("-wal"));
        const bool dbRestored = !databaseMoved || m_movePath(archivedDatabase, databaseFile);
        const bool userRestored = !userDirectoryMoved || m_movePath(archivedUserDirectory, userDirectory);
        return shmRestored && walRestored && dbRestored && userRestored;
    };
    const auto incompleteRollback = [&] {
        auto error = resetFailure(QStringLiteral("Profile rollback was incomplete. Recovery files were kept at %1.").arg(recoveryPath));
        error.error().userMessage = QStringLiteral("DevHub could not restore the entire profile. Recovery files were kept at %1.").arg(recoveryPath);
        return error;
    };
    if (!preserved) {
        if (!restoreProfile()) return incompleteRollback();
        auto reopened = m_database.open();
        if (!reopened) {
            return resetFailure(QStringLiteral("The old database could not be reopened after profile preservation failed."));
        }
        QDir(recoveryPath).removeRecursively();
        return resetFailure(QStringLiteral("The local profile database could not be preserved."));
    }

    auto opened = m_database.open();
    auto migrated = opened ? m_database.migrate() : shared::common::Result<void>::failure(opened.error());
    if (!migrated) {
        auto closedAgain = m_database.close();
        if (!closedAgain) {
            return resetFailure(QStringLiteral("The new database could not be closed while restoring the old profile."));
        }
        const bool removedDb = removeIfPresent(databaseFile);
        const bool removedWal = removeIfPresent(databaseFile + QStringLiteral("-wal"));
        const bool removedShm = removeIfPresent(databaseFile + QStringLiteral("-shm"));
        if (!removedDb || !removedWal || !removedShm) return incompleteRollback();
        if (!restoreProfile()) return incompleteRollback();
        auto reopened = m_database.open();
        if (!reopened) {
            return resetFailure(QStringLiteral("The old profile was restored but its database could not be reopened."));
        }
        QDir(recoveryPath).removeRecursively();
        return shared::common::Result<void>::failure(migrated.error());
    }

    return shared::common::Result<void>::success();
}

} // namespace devhub::infrastructure::filesystem
