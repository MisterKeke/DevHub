#pragma once

#include "../../application/planner/IPlannerRepository.h"
#include "../../domain/account/AccountId.h"
#include "../database/InternalDatabaseManager.h"

namespace devhub::infrastructure::planner
{

class SqlPlannerRepository final : public application::planner::IPlannerRepository
{
  public:
    SqlPlannerRepository(database::InternalDatabaseManager &database,
                         domain::account::AccountId accountId);

    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    createTask(domain::planner::PlannerTask task) override;
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    createImportedTask(domain::planner::PlannerTask task, bool allowAdditionalCopy) override;
    [[nodiscard]] shared::common::Result<QHash<QString, int>>
    importedSourceTaskCounts(const QString& workspaceId, const QString& sourceProfileIdentity) override;
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    getTask(const QString &workspaceId, const QString &taskId) override;
    [[nodiscard]] shared::common::Result<QVector<domain::planner::PlannerTask>>
    listTasks(const QString &workspaceId,
              const domain::planner::PlannerTaskFilter &filter) override;
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    updateTask(domain::planner::PlannerTask task, qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<void>
    removeTask(const QString &workspaceId, const QString &taskId, qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<void>
    reorderTasks(const QString &workspaceId, const QStringList &taskIds,
                 const QVector<qint64> &expectedRevisions,
                 domain::planner::PlannerStatus status) override;
    [[nodiscard]] shared::common::Result<void> setTags(const QString &workspaceId,
                                                       const QString &taskId,
                                                       const QStringList &tags,
                                                       qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<domain::planner::PlannerSubtask>
    addSubtask(const QString &workspaceId, const QString &taskId,
               domain::planner::PlannerSubtask subtask, qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<domain::planner::PlannerSubtask>
    updateSubtask(const QString &workspaceId, const QString &taskId,
                  domain::planner::PlannerSubtask subtask, qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<void> removeSubtask(const QString &workspaceId,
                                                             const QString &taskId,
                                                             const QString &subtaskId,
                                                             qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<void> reorderSubtasks(const QString &workspaceId,
                                                               const QString &taskId,
                                                               const QStringList &subtaskIds,
                                                               qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<domain::planner::PlannerMilestone>
    createMilestone(domain::planner::PlannerMilestone milestone) override;
    [[nodiscard]] shared::common::Result<QVector<domain::planner::PlannerMilestone>>
    listMilestones(const QString &workspaceId) override;
    [[nodiscard]] shared::common::Result<domain::planner::PlannerMilestone>
    updateMilestone(domain::planner::PlannerMilestone milestone, qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<void> removeMilestone(const QString &workspaceId,
                                                               const QString &milestoneId,
                                                               qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<void> addDependency(const QString &workspaceId,
                                                             const QString &taskId,
                                                             const QString &dependsOnTaskId,
                                                             qint64 expectedRevision) override;
    [[nodiscard]] shared::common::Result<void> removeDependency(const QString &workspaceId,
                                                                const QString &taskId,
                                                                const QString &dependsOnTaskId,
                                                                qint64 expectedRevision) override;

  private:
    [[nodiscard]] bool onOwnerThread() const;
    [[nodiscard]] shared::common::Result<domain::planner::PlannerTask>
    createTaskImpl(domain::planner::PlannerTask task, bool allowAdditionalCopy);
    database::InternalDatabaseManager &m_database;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::infrastructure::planner
