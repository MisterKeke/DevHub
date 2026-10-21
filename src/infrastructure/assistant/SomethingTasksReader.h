#pragma once

#include "../../application/tasks/Cancellation.h"
#include <QJsonObject>
#include <QStringList>

namespace devhub::infrastructure::assistant {

struct SomethingTasksReadResult final {
    bool success = false;
    bool cancelled = false;
    QString errorCode;
    QString message;
    QJsonObject data;
};

// A short-lived, read-only adapter for SomethingQt's task schema. Every call
// creates and destroys its QSQLITE connection on the calling worker thread.
class SomethingTasksReader final {
public:
    static SomethingTasksReadResult check(const QString& profilePath,
        const application::tasks::CancellationToken& cancellation = {});
    static SomethingTasksReadResult find(const QString& profilePath, QStringList tags,
        const QString& completion, int offset, int limit,
        const application::tasks::CancellationToken& cancellation = {});
    static SomethingTasksReadResult read(const QString& profilePath, const QString& taskId,
        const QString& requiredTag = {},
        const application::tasks::CancellationToken& cancellation = {});
};

} // namespace devhub::infrastructure::assistant
