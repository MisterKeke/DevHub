#include "PlannerTypes.h"

namespace devhub::domain::planner
{

QString plannerStatusName(PlannerStatus status)
{
    switch (status)
    {
    case PlannerStatus::Backlog:
        return QStringLiteral("backlog");
    case PlannerStatus::Todo:
        return QStringLiteral("todo");
    case PlannerStatus::InProgress:
        return QStringLiteral("in_progress");
    case PlannerStatus::InReview:
        return QStringLiteral("in_review");
    case PlannerStatus::Done:
        return QStringLiteral("done");
    }
    return QStringLiteral("todo");
}

std::optional<PlannerStatus> plannerStatusFromName(const QString &name)
{
    if (name == QStringLiteral("backlog"))
        return PlannerStatus::Backlog;
    if (name == QStringLiteral("todo"))
        return PlannerStatus::Todo;
    if (name == QStringLiteral("in_progress"))
        return PlannerStatus::InProgress;
    if (name == QStringLiteral("in_review"))
        return PlannerStatus::InReview;
    if (name == QStringLiteral("done"))
        return PlannerStatus::Done;
    return std::nullopt;
}

} // namespace devhub::domain::planner
