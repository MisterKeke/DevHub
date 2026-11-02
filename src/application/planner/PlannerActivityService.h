#pragma once

#include "IPlannerActivityRepository.h"
#include "../git/GitTypes.h"
#include "../../shared/time/ApplicationClock.h"

namespace devhub::application::planner
{

class PlannerActivityService final
{
  public:
    PlannerActivityService(IPlannerActivityRepository& repository, shared::time::IApplicationClock& clock)
        : m_repository(repository), m_clock(clock) {}

    [[nodiscard]] shared::common::Result<QVector<domain::planner::PlannerActivityEvent>>
    listEvents(const QString& workspaceId, const QString& taskId, int limit = 50, int offset = 0);
    [[nodiscard]] shared::common::Result<QVector<domain::planner::PlannerGitCommitAssociation>>
    listGitCommits(const QString& workspaceId, const QString& taskId);
    [[nodiscard]] shared::common::Result<void>
    linkGitCommit(const QString& workspaceId, const QString& taskId, const application::git::GitCommit& commit,
                  const QString& source, const QString& branchName = {});
    [[nodiscard]] shared::common::Result<void>
    unlinkGitCommit(const QString& workspaceId, const QString& taskId, const QString& sha);
    [[nodiscard]] shared::common::Result<void>
    setGitCommitAvailability(const QString& workspaceId, const QString& taskId, const QString& sha,
                             const QString& availability);
    [[nodiscard]] static QString taskReference(const QString& taskId);
    [[nodiscard]] static bool messageReferencesTask(const QString& message, const QString& taskId);

  private:
    IPlannerActivityRepository& m_repository;
    shared::time::IApplicationClock& m_clock;
};

} // namespace devhub::application::planner
