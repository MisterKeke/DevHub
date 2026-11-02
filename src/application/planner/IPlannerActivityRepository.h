#pragma once

#include "../../domain/planner/PlannerTypes.h"
#include "../../shared/common/Result.h"
#include <QDateTime>

namespace devhub::application::planner
{

class IPlannerActivityRepository
{
  public:
    virtual ~IPlannerActivityRepository() = default;
    [[nodiscard]] virtual shared::common::Result<QVector<domain::planner::PlannerActivityEvent>>
    listEvents(const QString& workspaceId, const QString& taskId, int limit, int offset) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<domain::planner::PlannerGitCommitAssociation>>
    listGitCommits(const QString& workspaceId, const QString& taskId) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    linkGitCommit(domain::planner::PlannerGitCommitAssociation association) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    unlinkGitCommit(const QString& workspaceId, const QString& taskId, const QString& sha) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    setGitCommitAvailability(const QString& workspaceId, const QString& taskId, const QString& sha,
                             const QString& availability, const QDateTime& observedAtUtc) = 0;
};

} // namespace devhub::application::planner
