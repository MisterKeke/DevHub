#pragma once

#include "PlannerTypes.h"

#include <QPair>

namespace devhub::domain::planner
{

class PlannerRules final
{
  public:
    [[nodiscard]] static PlannerTask withStatus(PlannerTask task, PlannerStatus status);
    [[nodiscard]] static PlannerTask withCompletion(PlannerTask task, bool completed);
    [[nodiscard]] static bool
    wouldCreateDependencyCycle(const QVector<QPair<QString, QString>> &edges, const QString &taskId,
                               const QString &dependsOnTaskId);
};

} // namespace devhub::domain::planner
