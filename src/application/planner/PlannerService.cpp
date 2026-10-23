#include "PlannerService.h"

#include "../../domain/planner/PlannerRules.h"

#include <algorithm>
#include <QUuid>

namespace devhub::application::planner
{
using domain::planner::PlannerMilestone;
using domain::planner::PlannerStatus;
using domain::planner::PlannerSubtask;
using domain::planner::PlannerTask;
using domain::planner::PlannerTaskFilter;
using shared::common::Result;

namespace
{
QDateTime nowUtc(shared::time::IApplicationClock &clock)
{
    return clock.utcNow().toUTC();
}
} // namespace

Result<PlannerTask> PlannerService::createTask(QString workspaceId, PlannerTask task)
{
    task.id = m_ids.create().toString(QUuid::WithoutBraces);
    task.workspaceId = std::move(workspaceId);
    task.revision = 1;
    task.createdAtUtc = nowUtc(m_clock);
    task.updatedAtUtc = task.createdAtUtc;
    if (task.sourceType == QStringLiteral("local"))
    {
        task.status = PlannerStatus::Todo;
        task.completed = false;
        task.sourceProfileIdentity.clear();
        task.sourceTaskId.clear();
        task.sourceRevision.clear();
        task.sourceCreatedAt.clear();
        task.sourceUpdatedAt.clear();
        task.importedAtUtc = {};
    }
    else
    {
        const bool sourceCompleted = task.completed;
        task = domain::planner::PlannerRules::withCompletion(std::move(task), sourceCompleted);
        task.importedAtUtc =
            task.importedAtUtc.isValid() ? task.importedAtUtc.toUTC() : task.createdAtUtc;
    }
    for (PlannerSubtask &subtask : task.subtasks)
    {
        if (subtask.id.isEmpty())
            subtask.id = m_ids.create().toString(QUuid::WithoutBraces);
    }
    auto validation = m_validation.validateTask(task);
    if (!validation)
        return Result<PlannerTask>::failure(validation.error());
    return m_repository.createTask(std::move(task));
}

Result<PlannerTask> PlannerService::importTask(QString workspaceId, PlannerTask task,
                                               bool allowAdditionalCopy)
{
    task.id = m_ids.create().toString(QUuid::WithoutBraces);
    task.workspaceId = std::move(workspaceId);
    task.sourceType = QStringLiteral("somethingqt");
    task.revision = 1;
    task.createdAtUtc = nowUtc(m_clock);
    task.updatedAtUtc = task.createdAtUtc;
    task.status = task.completed ? PlannerStatus::Done : PlannerStatus::Todo;
    task.importedAtUtc = task.createdAtUtc;
    for (PlannerSubtask& subtask : task.subtasks)
        subtask.id = m_ids.create().toString(QUuid::WithoutBraces);
    auto validation = m_validation.validateTask(task);
    if (!validation) return Result<PlannerTask>::failure(validation.error());
    return m_repository.createImportedTask(std::move(task), allowAdditionalCopy);
}

Result<QHash<QString, int>> PlannerService::importedSourceTaskCounts(
    const QString& workspaceId, const QString& sourceProfileIdentity)
{
    return m_repository.importedSourceTaskCounts(workspaceId, sourceProfileIdentity);
}

Result<PlannerTask> PlannerService::getTask(const QString &workspaceId, const QString &taskId)
{
    return m_repository.getTask(workspaceId, taskId);
}

Result<QVector<PlannerTask>> PlannerService::listTasks(const QString &workspaceId,
                                                       const PlannerTaskFilter &filter)
{
    auto validation = m_validation.validateFilter(filter);
    if (!validation)
        return Result<QVector<PlannerTask>>::failure(validation.error());
    return m_repository.listTasks(workspaceId, filter);
}

Result<PlannerTask> PlannerService::updateTask(PlannerTask task, qint64 expectedRevision)
{
    if (expectedRevision < 1)
    {
        return Result<PlannerTask>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("The task revision is invalid."),
            QStringLiteral("Expected revision must be positive."),
            QStringLiteral("update Planner task")));
    }
    const PlannerStatus requestedStatus = task.status;
    task = domain::planner::PlannerRules::withStatus(std::move(task), requestedStatus);
    auto validation = m_validation.validateTask(task);
    if (!validation)
        return Result<PlannerTask>::failure(validation.error());
    task.updatedAtUtc = nowUtc(m_clock);
    return m_repository.updateTask(std::move(task), expectedRevision);
}

Result<PlannerTask> PlannerService::moveTask(const QString &workspaceId, const QString &taskId,
                                             qint64 expectedRevision, PlannerStatus status)
{
    auto task = m_repository.getTask(workspaceId, taskId);
    if (!task)
        return task;
    PlannerTask updated =
        domain::planner::PlannerRules::withStatus(std::move(task).value(), status);
    return updateTask(std::move(updated), expectedRevision);
}

Result<PlannerTask> PlannerService::setCompleted(const QString &workspaceId, const QString &taskId,
                                                 qint64 expectedRevision, bool completed)
{
    auto task = m_repository.getTask(workspaceId, taskId);
    if (!task)
        return task;
    PlannerTask updated =
        domain::planner::PlannerRules::withCompletion(std::move(task).value(), completed);
    return updateTask(std::move(updated), expectedRevision);
}

Result<void> PlannerService::removeTask(const QString &workspaceId, const QString &taskId,
                                        qint64 expectedRevision)
{
    return m_repository.removeTask(workspaceId, taskId, expectedRevision);
}

Result<void> PlannerService::reorderTasks(const QString &workspaceId, const QStringList &taskIds,
                                          const QVector<qint64> &expectedRevisions,
                                          PlannerStatus status)
{
    return m_repository.reorderTasks(workspaceId, taskIds, expectedRevisions, status);
}

Result<void> PlannerService::setTags(const QString &workspaceId, const QString &taskId,
                                     const QStringList &tags, qint64 expectedRevision)
{
    return m_repository.setTags(workspaceId, taskId, tags, expectedRevision);
}

Result<PlannerSubtask> PlannerService::addSubtask(const QString &workspaceId, const QString &taskId,
                                                  QString title, int position,
                                                  qint64 expectedRevision)
{
    PlannerSubtask subtask;
    subtask.id = m_ids.create().toString(QUuid::WithoutBraces);
    subtask.title = std::move(title);
    subtask.position = position;
    return m_repository.addSubtask(workspaceId, taskId, std::move(subtask), expectedRevision);
}

Result<PlannerSubtask> PlannerService::updateSubtask(const QString &workspaceId,
                                                     const QString &taskId, PlannerSubtask subtask,
                                                     qint64 expectedRevision)
{
    return m_repository.updateSubtask(workspaceId, taskId, std::move(subtask), expectedRevision);
}

Result<void> PlannerService::removeSubtask(const QString &workspaceId, const QString &taskId,
                                           const QString &subtaskId, qint64 expectedRevision)
{
    return m_repository.removeSubtask(workspaceId, taskId, subtaskId, expectedRevision);
}

Result<void> PlannerService::reorderSubtasks(const QString &workspaceId, const QString &taskId,
                                             const QStringList &subtaskIds, qint64 expectedRevision)
{
    return m_repository.reorderSubtasks(workspaceId, taskId, subtaskIds, expectedRevision);
}

Result<domain::planner::PlannerTimeEntry> PlannerService::addTimeEntry(
    QString workspaceId, QString taskId, qint64 durationMinutes, QString note,
    qint64 expectedRevision)
{
    domain::planner::PlannerTimeEntry entry;
    entry.workspaceId = std::move(workspaceId);
    entry.taskId = std::move(taskId);
    entry.durationMinutes = durationMinutes;
    entry.note = std::move(note);
    entry.id = m_ids.create().toString(QUuid::WithoutBraces);
    entry.recordedAtUtc = nowUtc(m_clock);
    auto validation = m_validation.validateTimeEntry(entry);
    if (!validation) return Result<domain::planner::PlannerTimeEntry>::failure(validation.error());
    return m_repository.addTimeEntry(std::move(entry), expectedRevision);
}

Result<QVector<domain::planner::PlannerTimeEntry>> PlannerService::listTimeEntries(
    const QString &workspaceId, const QString &taskId)
{
    return m_repository.listTimeEntries(workspaceId, taskId);
}

Result<void> PlannerService::removeTimeEntry(const QString &workspaceId, const QString &taskId,
                                             const QString &entryId, qint64 expectedRevision)
{
    return m_repository.removeTimeEntry(workspaceId, taskId, entryId, expectedRevision);
}

Result<domain::planner::PlannerActionRun> PlannerService::beginActionRun(
    const QString& workspaceId, const QString& taskId, QString actionId, QString logReference)
{
    const QString actionPrefix = QStringLiteral("service.start:");
    const QString serviceId = actionId.startsWith(actionPrefix) ? actionId.mid(actionPrefix.size()) : QString{};
    if (serviceId.isEmpty() || QUuid::fromString(serviceId).isNull())
        return Result<domain::planner::PlannerActionRun>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("This Planner action is not supported."), actionId,
            QStringLiteral("start Planner action")));
    auto task = m_repository.getTask(workspaceId, taskId);
    if (!task) return Result<domain::planner::PlannerActionRun>::failure(task.error());
    if (task.value().linkedServiceId != serviceId)
        return Result<domain::planner::PlannerActionRun>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("Link a configured service to this task first."), {},
            QStringLiteral("start Planner action")));
    domain::planner::PlannerActionRun run;
    run.id = m_ids.create().toString(QUuid::WithoutBraces);
    run.workspaceId = workspaceId;
    run.taskId = taskId;
    run.actionId = std::move(actionId);
    run.startedAtUtc = nowUtc(m_clock);
    run.logReference = std::move(logReference);
    return m_repository.beginActionRun(std::move(run));
}

Result<void> PlannerService::finishActionRun(const QString& workspaceId, const QString& taskId,
                                              const QString& runId, bool succeeded,
                                              QString errorSummary)
{
    auto runs = m_repository.listActionRuns(workspaceId, taskId, 20);
    if (!runs) return Result<void>::failure(runs.error());
    const auto run = std::find_if(runs.value().cbegin(), runs.value().cend(), [&runId](const auto& row) {
        return row.id == runId && !row.finishedAtUtc.isValid() && !row.succeeded.has_value();
    });
    if (run == runs.value().cend())
        return Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::RevisionConflict, domain::errors::ErrorCategory::Persistence,
            QStringLiteral("This Planner action has already finished or is unavailable."), runId,
            QStringLiteral("finish Planner action")));
    const qint64 duration = qMax<qint64>(0, run->startedAtUtc.msecsTo(nowUtc(m_clock)));
    return m_repository.finishActionRun(workspaceId, taskId, runId, succeeded, duration,
                                        errorSummary.left(512));
}

Result<QVector<domain::planner::PlannerActionRun>> PlannerService::listActionRuns(
    const QString& workspaceId, const QString& taskId, int limit)
{
    return m_repository.listActionRuns(workspaceId, taskId, qBound(1, limit, 20));
}

Result<PlannerMilestone> PlannerService::createMilestone(QString workspaceId,
                                                         PlannerMilestone milestone)
{
    milestone.id = m_ids.create().toString(QUuid::WithoutBraces);
    milestone.workspaceId = std::move(workspaceId);
    milestone.revision = 1;
    milestone.createdAtUtc = nowUtc(m_clock);
    milestone.updatedAtUtc = milestone.createdAtUtc;
    auto validation = m_validation.validateMilestone(milestone);
    if (!validation)
        return Result<PlannerMilestone>::failure(validation.error());
    return m_repository.createMilestone(std::move(milestone));
}

Result<QVector<PlannerMilestone>> PlannerService::listMilestones(const QString &workspaceId)
{
    return m_repository.listMilestones(workspaceId);
}

Result<PlannerMilestone> PlannerService::updateMilestone(PlannerMilestone milestone,
                                                         qint64 expectedRevision)
{
    auto validation = m_validation.validateMilestone(milestone);
    if (!validation)
        return Result<PlannerMilestone>::failure(validation.error());
    milestone.updatedAtUtc = nowUtc(m_clock);
    return m_repository.updateMilestone(std::move(milestone), expectedRevision);
}

Result<void> PlannerService::removeMilestone(const QString &workspaceId, const QString &milestoneId,
                                             qint64 expectedRevision)
{
    return m_repository.removeMilestone(workspaceId, milestoneId, expectedRevision);
}

Result<void> PlannerService::addDependency(const QString &workspaceId, const QString &taskId,
                                           const QString &dependsOnTaskId, qint64 expectedRevision)
{
    return m_repository.addDependency(workspaceId, taskId, dependsOnTaskId, expectedRevision);
}

Result<void> PlannerService::removeDependency(const QString &workspaceId, const QString &taskId,
                                              const QString &dependsOnTaskId,
                                              qint64 expectedRevision)
{
    return m_repository.removeDependency(workspaceId, taskId, dependsOnTaskId, expectedRevision);
}

} // namespace devhub::application::planner
