#pragma once

#include "../../shared/identifiers/IdGenerator.h"
#include "../../shared/time/ApplicationClock.h"
#include "IPlannerRepository.h"
#include "PlannerValidation.h"

namespace devhub::application::planner
{

class PlannerService final
{
  public:
    PlannerService(IPlannerRepository &repository, const PlannerValidation &validation,
                   shared::identifiers::IIdGenerator &ids, shared::time::IApplicationClock &clock)
        : m_repository(repository), m_validation(validation), m_ids(ids), m_clock(clock)
    {
    }

    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    createTask(QString workspaceId, domain::planner::PlannerTask task);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    importTask(QString workspaceId, domain::planner::PlannerTask task, bool allowAdditionalCopy = false);
    [[nodiscard]] shared::common::Result<QHash<QString, int>>
    importedSourceTaskCounts(const QString& workspaceId, const QString& sourceProfileIdentity);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    getTask(const QString &workspaceId, const QString &taskId);
    [[nodiscard]] shared::common::Result<QVector<domain::planner::PlannerTask>>
    listTasks(const QString &workspaceId, const domain::planner::PlannerTaskFilter &filter);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    updateTask(domain::planner::PlannerTask task, qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    moveTask(const QString &workspaceId, const QString &taskId, qint64 expectedRevision,
             domain::planner::PlannerStatus status);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    setCompleted(const QString &workspaceId, const QString &taskId, qint64 expectedRevision,
                 bool completed);
    [[nodiscard]] shared::common::Result<void>
    removeTask(const QString &workspaceId, const QString &taskId, qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<void>
    reorderTasks(const QString &workspaceId, const QStringList &taskIds,
                 const QVector<qint64> &expectedRevisions, domain::planner::PlannerStatus status);
    [[nodiscard]] shared::common::Result<void> setTags(const QString &workspaceId,
                                                       const QString &taskId,
                                                       const QStringList &tags,
                                                       qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerSubtask>
    addSubtask(const QString &workspaceId, const QString &taskId, QString title, int position,
               qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerSubtask>
    updateSubtask(const QString &workspaceId, const QString &taskId,
                  domain::planner::PlannerSubtask subtask, qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<void> removeSubtask(const QString &workspaceId,
                                                             const QString &taskId,
                                                             const QString &subtaskId,
                                                             qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<void> reorderSubtasks(const QString &workspaceId,
                                                               const QString &taskId,
                                                               const QStringList &subtaskIds,
                                                               qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerMilestone>
    createMilestone(QString workspaceId, domain::planner::PlannerMilestone milestone);
    [[nodiscard]] shared::common::Result<QVector<domain::planner::PlannerMilestone>>
    listMilestones(const QString &workspaceId);
    [[nodiscard]] shared::common::Result<domain::planner::PlannerMilestone>
    updateMilestone(domain::planner::PlannerMilestone milestone, qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<void> removeMilestone(const QString &workspaceId,
                                                               const QString &milestoneId,
                                                               qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<void> addDependency(const QString &workspaceId,
                                                             const QString &taskId,
                                                             const QString &dependsOnTaskId,
                                                             qint64 expectedRevision);
    [[nodiscard]] shared::common::Result<void> removeDependency(const QString &workspaceId,
                                                                const QString &taskId,
                                                                const QString &dependsOnTaskId,
                                                                qint64 expectedRevision);

  private:
    IPlannerRepository &m_repository;
    const PlannerValidation &m_validation;
    shared::identifiers::IIdGenerator &m_ids;
    shared::time::IApplicationClock &m_clock;
};

} // namespace devhub::application::planner
