#pragma once

#include "../../application/planner/IPlannerActivityRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../domain/account/AccountId.h"

namespace devhub::infrastructure::planner
{

class SqlPlannerActivityRepository final : public application::planner::IPlannerActivityRepository
{
  public:
    SqlPlannerActivityRepository(database::InternalDatabaseManager& database,
                                 domain::account::AccountId accountId)
        : m_database(database), m_accountId(std::move(accountId)) {}
    shared::common::Result<QVector<domain::planner::PlannerActivityEvent>>
    listEvents(const QString& workspaceId, const QString& taskId, int limit, int offset) override;
    shared::common::Result<QVector<domain::planner::PlannerGitCommitAssociation>>
    listGitCommits(const QString& workspaceId, const QString& taskId) override;
    shared::common::Result<void>
    linkGitCommit(domain::planner::PlannerGitCommitAssociation association) override;
    shared::common::Result<void>
    unlinkGitCommit(const QString& workspaceId, const QString& taskId, const QString& sha) override;
    shared::common::Result<void>
    setGitCommitAvailability(const QString& workspaceId, const QString& taskId, const QString& sha,
                             const QString& availability, const QDateTime& observedAtUtc) override;

  private:
    [[nodiscard]] bool onOwnerThread() const;
    database::InternalDatabaseManager& m_database;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::infrastructure::planner
