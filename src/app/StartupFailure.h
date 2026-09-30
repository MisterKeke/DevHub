#pragma once

#include "../domain/errors/AppError.h"

#include <QVector>
#include <QString>

namespace devhub::app {

enum class StartupRecoveryAction
{
    Retry,
    OpenDataDirectory,
    OpenLogDirectory,
    Quit
};

struct StartupFailure final
{
    domain::errors::AppError error;
    QVector<StartupRecoveryAction> availableActions;
    QString recoverableFilePath;
    QString recoveryCopyDirectory;
};

} // namespace devhub::app
