#pragma once

#include "../../domain/planner/PlannerTypes.h"
#include "../../shared/common/Result.h"

#include <QHash>

namespace devhub::application::planner
{

using domain::planner::PlannerMilestone;
using domain::planner::PlannerSubtask;
using domain::planner::PlannerTask;
using domain::planner::PlannerTaskFilter;
using domain::planner::PlannerTimeEntry;

class IPlannerRepository
{
  public:
    virtual ~IPlannerRepository() = default;
    [[nodiscard]] virtual shared::common::Result<PlannerTask> createTask(PlannerTask task) = 0;
    [[nodiscard]] virtual shared::common::Result<PlannerTask>
    createImportedTask(PlannerTask task, bool allowAdditionalCopy) = 0;
    [[nodiscard]] virtual shared::common::Result<QHash<QString, int>>
    importedSourceTaskCounts(const QString& workspaceId, const QString& sourceProfileIdentity) = 0;
    [[nodiscard]] virtual shared::common::Result<PlannerTask> getTask(const QString &workspaceId,
                                                                      const QString &taskId) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<PlannerTask>>
    listTasks(const QString &workspaceId, const PlannerTaskFilter &filter) = 0;
    [[nodiscard]] virtual shared::common::Result<PlannerTask>
    updateTask(PlannerTask task, qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    removeTask(const QString &workspaceId, const QString &taskId, qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    reorderTasks(const QString &workspaceId, const QStringList &taskIds,
                 const QVector<qint64> &expectedRevisions,
                 domain::planner::PlannerStatus status) = 0;
    [[nodiscard]] virtual shared::common::Result<void> setTags(const QString &workspaceId,
                                                               const QString &taskId,
                                                               const QStringList &tags,
                                                               qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<PlannerSubtask>
    addSubtask(const QString &workspaceId, const QString &taskId, PlannerSubtask subtask,
               qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<PlannerSubtask>
    updateSubtask(const QString &workspaceId, const QString &taskId, PlannerSubtask subtask,
                  qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<void> removeSubtask(const QString &workspaceId,
                                                                     const QString &taskId,
                                                                     const QString &subtaskId,
                                                                     qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    reorderSubtasks(const QString &workspaceId, const QString &taskId,
                    const QStringList &subtaskIds, qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<PlannerTimeEntry>
    addTimeEntry(PlannerTimeEntry entry, qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<PlannerTimeEntry>>
    listTimeEntries(const QString &workspaceId, const QString &taskId) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    removeTimeEntry(const QString &workspaceId, const QString &taskId,
                    const QString &entryId, qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<PlannerMilestone>
    createMilestone(PlannerMilestone milestone) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<PlannerMilestone>>
    listMilestones(const QString &workspaceId) = 0;
    [[nodiscard]] virtual shared::common::Result<PlannerMilestone>
    updateMilestone(PlannerMilestone milestone, qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<void> removeMilestone(const QString &workspaceId,
                                                                       const QString &milestoneId,
                                                                       qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<void> addDependency(const QString &workspaceId,
                                                                     const QString &taskId,
                                                                     const QString &dependsOnTaskId,
                                                                     qint64 expectedRevision) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    removeDependency(const QString &workspaceId, const QString &taskId,
                     const QString &dependsOnTaskId, qint64 expectedRevision) = 0;
};

} // namespace devhub::application::planner
