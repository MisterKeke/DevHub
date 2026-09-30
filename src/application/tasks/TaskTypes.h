#pragma once

#include "../../domain/errors/AppError.h"
#include "Cancellation.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QUuid>

#include <functional>
#include <optional>

namespace devhub::application::tasks {

using TaskId = QUuid;
using TaskGeneration = quint64;

struct TaskProgress final
{
    TaskId taskId;
    domain::errors::CorrelationId correlationId;
    QString moduleKey;
    QString message;
    int percentage = -1;
    std::optional<QUuid> workspaceId;
    TaskGeneration generation = 0;
};

struct TaskRequest final
{
    QString moduleKey;
    std::optional<QUuid> workspaceId;
    TaskGeneration generation = 0;
    domain::errors::CorrelationId correlationId;
};

struct TaskContext final
{
    TaskId taskId;
    TaskRequest request;
    domain::errors::CorrelationId correlationId;
    CancellationToken cancellationToken;
    std::function<void(QString, int)> reportProgress;
};

} // namespace devhub::application::tasks

Q_DECLARE_METATYPE(devhub::application::tasks::TaskProgress)
