#pragma once

#include "../../domain/planner/PlannerTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::planner
{

class PlannerValidation final
{
  public:
    [[nodiscard]] shared::common::Result<void>
    validateTask(const domain::planner::PlannerTask &task) const;
    [[nodiscard]] shared::common::Result<void>
    validateMilestone(const domain::planner::PlannerMilestone &milestone) const;
    [[nodiscard]] shared::common::Result<void>
    validateTimeEntry(const domain::planner::PlannerTimeEntry &entry) const;
    [[nodiscard]] shared::common::Result<void>
    validateFilter(const domain::planner::PlannerTaskFilter &filter) const;
    [[nodiscard]] shared::common::Result<void> validateGitHubUrl(const QString &value) const;
    [[nodiscard]] shared::common::Result<void> validateGitHubIssueUrl(const QString &value) const;
    [[nodiscard]] shared::common::Result<void> validateGitHubPullRequestUrl(const QString &value) const;
};

} // namespace devhub::application::planner
