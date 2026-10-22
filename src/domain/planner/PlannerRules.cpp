#include "PlannerRules.h"

#include <QHash>
#include <QSet>

namespace devhub::domain::planner
{

PlannerTask PlannerRules::withStatus(PlannerTask task, PlannerStatus status)
{
    task.status = status;
    task.completed = status == PlannerStatus::Done;
    return task;
}

PlannerTask PlannerRules::withCompletion(PlannerTask task, bool completed)
{
    if (completed)
    {
        task.status = PlannerStatus::Done;
        task.completed = true;
    }
    else
    {
        if (task.status == PlannerStatus::Done)
            task.status = PlannerStatus::Todo;
        task.completed = false;
    }
    return task;
}

bool PlannerRules::wouldCreateDependencyCycle(const QVector<QPair<QString, QString>> &edges,
                                              const QString &taskId, const QString &dependsOnTaskId)
{
    if (taskId == dependsOnTaskId)
        return true;
    QHash<QString, QStringList> adjacency;
    for (const auto &edge : edges)
        adjacency[edge.first].append(edge.second);

    QVector<QString> pending{dependsOnTaskId};
    QSet<QString> visited;
    while (!pending.isEmpty())
    {
        const QString current = pending.takeLast();
        if (current == taskId)
            return true;
        if (visited.contains(current))
            continue;
        visited.insert(current);
        pending.append(adjacency.value(current));
    }
    return false;
}

} // namespace devhub::domain::planner
