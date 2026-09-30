#include "ApplicationPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace devhub::infrastructure::filesystem {
namespace {

shared::common::Result<void> ensureWritableDirectory(const QString& path)
{
    QDir directory;
    if (!directory.mkpath(path)) {
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::DirectoryCreationFailed,
            domain::errors::ErrorCategory::FileSystem,
            QStringLiteral("DevHub could not prepare its local data folders."),
            QStringLiteral("Unable to create directory: %1").arg(path),
            QStringLiteral("prepare application paths")));
    }

    QTemporaryFile probe(QDir(path).filePath(QStringLiteral(".devhub-write-test-XXXXXX")));
    probe.setAutoRemove(true);
    if (!probe.open()) {
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::PathNotWritable,
            domain::errors::ErrorCategory::Permission,
            QStringLiteral("DevHub cannot write to one of its required data folders."),
            QStringLiteral("Unable to create write probe in %1: %2").arg(path, probe.errorString()),
            QStringLiteral("validate application paths")));
    }
    probe.close();
    return shared::common::Result<void>::success();
}

} // namespace

shared::common::Result<ApplicationPaths> ApplicationPaths::resolveAndPrepare()
{
    ApplicationPaths paths;
    paths.localDataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    paths.roamingConfigDirectory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    paths.cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    if (paths.localDataDirectory.isEmpty() || paths.roamingConfigDirectory.isEmpty()
        || paths.cacheDirectory.isEmpty()) {
        return shared::common::Result<ApplicationPaths>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::PathUnavailable,
            domain::errors::ErrorCategory::Configuration,
            QStringLiteral("DevHub could not locate the folders it needs."),
            QStringLiteral("QStandardPaths returned an empty application data, config, or cache path."),
            QStringLiteral("resolve application paths")));
    }

    paths.logDirectory = QDir(paths.localDataDirectory).filePath(QStringLiteral("logs"));
    paths.dataDirectory = QDir(paths.localDataDirectory).filePath(QStringLiteral("data"));
    paths.internalDatabaseFile = QDir(paths.dataDirectory).filePath(QStringLiteral("devhub.db"));
    paths.backupDirectory = QDir(paths.localDataDirectory).filePath(QStringLiteral("backups"));
    paths.usersDirectory = QDir(paths.localDataDirectory).filePath(QStringLiteral("users"));

    const QStringList requiredDirectories = {
        paths.localDataDirectory,
        paths.dataDirectory,
        paths.roamingConfigDirectory,
        paths.logDirectory,
        paths.cacheDirectory,
        paths.backupDirectory,
        paths.usersDirectory
    };

    for (const QString& directory : requiredDirectories) {
        auto result = ensureWritableDirectory(directory);
        if (!result) {
            return shared::common::Result<ApplicationPaths>::failure(result.error());
        }
    }

    return shared::common::Result<ApplicationPaths>::success(std::move(paths));
}

} // namespace devhub::infrastructure::filesystem
