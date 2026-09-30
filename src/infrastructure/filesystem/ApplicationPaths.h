#pragma once

#include "../../shared/common/Result.h"

#include <QString>

namespace devhub::infrastructure::filesystem {

struct ApplicationPaths final
{
    QString localDataDirectory;
    QString dataDirectory;
    QString internalDatabaseFile;
    QString roamingConfigDirectory;
    QString logDirectory;
    QString cacheDirectory;
    QString backupDirectory;
    QString usersDirectory;

    [[nodiscard]] static shared::common::Result<ApplicationPaths> resolveAndPrepare();
};

} // namespace devhub::infrastructure::filesystem
