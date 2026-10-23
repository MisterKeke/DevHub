#pragma once

#include "../../application/tasks/Cancellation.h"
#include "../../domain/planner/PlannerTypes.h"

#include <QStringList>
#include <functional>

namespace devhub::infrastructure::planner {

struct SomethingQtImportProfile final {
    QString identity;
    int schemaVersion = 0;
    qint64 taskCount = 0;
    QStringList tags;
};

struct SomethingQtImportSummary final {
    QString sourceTaskId;
    QString title;
    bool completed = false;
    QString priority;
    QString difficulty;
    QString dueDate;
    QString createdAt;
    QString updatedAt;
    QString revision;
    QStringList tags;
};

struct SomethingQtImportFilter final {
    QString searchText;
    QString tag;
    QString priority;
    int completion = -1; // -1 all, 0 incomplete, 1 complete
    int offset = 0;
    int limit = 50;
};

struct SomethingQtImportSelection final {
    bool all = false;
    QStringList sourceTaskIds;
    QStringList tags; // Match any of the selected tags.
};

template<typename T>
struct SomethingQtImportRead final {
    bool success = false;
    bool cancelled = false;
    QString errorCode;
    QString message;
    T value{};
};

struct SomethingQtImportPage final {
    QVector<SomethingQtImportSummary> tasks;
    qint64 totalCount = 0;
};

struct SomethingQtImportStream final {
    bool success = false;
    bool cancelled = false;
    QString errorCode;
    QString message;
    qint64 sourceCount = 0;
    qint64 processedCount = 0;
};

// Full-data, short-lived adapter for SomethingQt. Every call owns a separate
// read-only QSQLITE connection in the calling thread. The assistant reader has
// a different bounded JSON contract and remains independent.
class SomethingQtImportReader final {
public:
    using TaskCallback = std::function<bool(domain::planner::PlannerTask)>;
    using ProgressCallback = std::function<void(qint64, qint64)>;

    [[nodiscard]] static SomethingQtImportRead<SomethingQtImportProfile>
    inspect(const QString& path, const application::tasks::CancellationToken& cancellation = {});
    [[nodiscard]] static SomethingQtImportRead<SomethingQtImportPage>
    listPage(const QString& path, const QString& expectedIdentity,
             const SomethingQtImportFilter& filter,
             const application::tasks::CancellationToken& cancellation = {});
    [[nodiscard]] static SomethingQtImportRead<domain::planner::PlannerTask>
    readTask(const QString& path, const QString& expectedIdentity, const QString& sourceTaskId,
             const application::tasks::CancellationToken& cancellation = {});
    [[nodiscard]] static SomethingQtImportRead<qint64>
    countSelection(const QString& path, const QString& expectedIdentity,
                   const SomethingQtImportSelection& selection,
                   const application::tasks::CancellationToken& cancellation = {});
    [[nodiscard]] static SomethingQtImportStream
    forEachTask(const QString& path, const QString& expectedIdentity,
                const SomethingQtImportSelection& selection,
                const application::tasks::CancellationToken& cancellation,
                const TaskCallback& callback, const ProgressCallback& progress = {});
};

} // namespace devhub::infrastructure::planner
