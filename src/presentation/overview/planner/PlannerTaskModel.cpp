#include "PlannerTaskModel.h"

#include <QDate>
#include <QVariantMap>
#include <algorithm>

namespace devhub::presentation::overview::planner {
using domain::planner::PlannerTask;

PlannerTaskModel::PlannerTaskModel(QObject* parent) : QAbstractListModel(parent) {}
int PlannerTaskModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_tasks.size()); }

QVariant PlannerTaskModel::data(const QModelIndex& index, int role) const
{
    const auto* task = taskAt(index.row());
    if (!task || !index.isValid()) return {};
    QVariantList subtasks;
    for (const auto& subtask : task->subtasks)
        subtasks.append(QVariantMap{{QStringLiteral("id"), subtask.id},
            {QStringLiteral("title"), subtask.title}, {QStringLiteral("completed"), subtask.completed},
            {QStringLiteral("position"), subtask.position}});
    switch (role) {
    case IdRole: return task->id;
    case TitleRole: return task->title;
    case DescriptionRole: return task->description;
    case CompletedRole: return task->completed;
    case StatusRole: return domain::planner::plannerStatusName(task->status);
    case StatusLabelRole: {
        const auto name = domain::planner::plannerStatusName(task->status).replace(QLatin1Char('_'), QLatin1Char(' '));
        QString label = name; if (!label.isEmpty()) label[0] = label[0].toUpper(); return label;
    }
    case BoardPositionRole: return task->boardPosition;
    case DueDateRole: return task->dueDate.isValid() ? task->dueDate.toString(Qt::ISODate) : QString{};
    case StartDateRole: return task->startDate.isValid() ? task->startDate.toString(Qt::ISODate) : QString{};
    case PriorityRole: return task->priority;
    case DifficultyRole: return task->difficulty;
    case RevisionRole: return task->revision;
    case EstimatedMinutesRole: return task->estimatedMinutes ? QVariant(*task->estimatedMinutes) : QVariant{};
    case ActualTrackedMinutesRole: return task->actualTrackedMinutes;
    case MilestoneIdRole: return task->milestoneId;
    case GitBranchRole: return task->gitBranch;
    case IssueUrlRole: return task->githubIssueUrl;
    case PullRequestUrlRole: return task->githubPullRequestUrl;
    case NotesRole: return task->notes;
    case BlockedReasonRole: return task->blockedReason;
    case LinkedServiceIdRole: return task->linkedServiceId;
    case SourceTypeRole: return task->sourceType;
    case SourceProfileIdentityRole: return task->sourceProfileIdentity;
    case SourceTaskIdRole: return task->sourceTaskId;
    case SourceRevisionRole: return task->sourceRevision;
    case SourceCreatedAtRole: return task->sourceCreatedAt;
    case SourceUpdatedAtRole: return task->sourceUpdatedAt;
    case ImportedAtRole: return task->importedAtUtc.isValid() ? task->importedAtUtc.toString(Qt::ISODateWithMs) : QString{};
    case TagsRole: return task->tags;
    case SubtasksRole: return subtasks;
    case DependencyIdsRole: return task->dependencyIds;
    case BlockedByDependencyRole: return task->blockedByDependency;
    case BlockedRole: return task->blockedByDependency || !task->blockedReason.isEmpty();
    case IncompletePrerequisiteTitlesRole: return task->incompletePrerequisiteTitles;
    case DependentTaskIdsRole: return task->dependentTaskIds;
    case PrerequisiteTitlesRole: return task->prerequisiteTitles;
    case DependentTaskTitlesRole: return task->dependentTaskTitles;
    case OverdueRole: return !task->completed && task->dueDate.isValid() && task->dueDate < QDate::currentDate();
    case SubtaskCountRole: return task->subtasks.size();
    case CompletedSubtaskCountRole: return std::count_if(task->subtasks.cbegin(), task->subtasks.cend(), [](const auto& item) { return item.completed; });
    case CreatedAtRole: return task->createdAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
    case UpdatedAtRole: return task->updatedAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
    default: return {};
    }
}

QHash<int, QByteArray> PlannerTaskModel::roleNames() const
{
    return {{IdRole,"taskId"},{TitleRole,"title"},{DescriptionRole,"description"},
        {CompletedRole,"completed"},{StatusRole,"status"},{StatusLabelRole,"statusLabel"},
        {BoardPositionRole,"boardPosition"},{DueDateRole,"dueDate"},{StartDateRole,"startDate"},
        {PriorityRole,"priority"},{DifficultyRole,"difficulty"},{RevisionRole,"revision"},
        {EstimatedMinutesRole,"estimatedMinutes"},{ActualTrackedMinutesRole,"actualTrackedMinutes"},
        {MilestoneIdRole,"milestoneId"},{GitBranchRole,"gitBranch"},{IssueUrlRole,"githubIssueUrl"},
        {PullRequestUrlRole,"githubPullRequestUrl"},{NotesRole,"notes"},{BlockedReasonRole,"blockedReason"},
        {SourceTypeRole,"sourceType"},{SourceProfileIdentityRole,"sourceProfileIdentity"},
        {SourceTaskIdRole,"sourceTaskId"},{SourceRevisionRole,"sourceRevision"},
        {SourceCreatedAtRole,"sourceCreatedAt"},{SourceUpdatedAtRole,"sourceUpdatedAt"},
        {ImportedAtRole,"importedAt"},{TagsRole,"tags"},{SubtasksRole,"subtasks"},
        {DependencyIdsRole,"dependencyIds"},{OverdueRole,"overdue"},{SubtaskCountRole,"subtaskCount"},
        {CompletedSubtaskCountRole,"completedSubtaskCount"},{CreatedAtRole,"createdAt"},
        {UpdatedAtRole,"updatedAt"},{BlockedByDependencyRole,"blockedByDependency"},
        {IncompletePrerequisiteTitlesRole,"incompletePrerequisiteTitles"},{DependentTaskIdsRole,"dependentTaskIds"},
        {PrerequisiteTitlesRole,"prerequisiteTitles"},{DependentTaskTitlesRole,"dependentTaskTitles"},
        {BlockedRole,"blocked"},{LinkedServiceIdRole,"linkedServiceId"}};
}

const PlannerTask* PlannerTaskModel::taskAt(int row) const
{ return row >= 0 && row < m_tasks.size() ? &m_tasks.at(row) : nullptr; }
const PlannerTask* PlannerTaskModel::taskById(const QString& id) const
{ const auto it = std::find_if(m_tasks.cbegin(), m_tasks.cend(), [&id](const auto& task) { return task.id == id; }); return it == m_tasks.cend() ? nullptr : &*it; }
void PlannerTaskModel::setTasks(QVector<PlannerTask> tasks)
{
    beginResetModel(); m_tasks = std::move(tasks); endResetModel();
}
void PlannerTaskModel::clear() { setTasks({}); }

PlannerTaskFilterModel::PlannerTaskFilterModel(QObject* parent) : QSortFilterProxyModel(parent)
{ setDynamicSortFilter(true); sort(0, Qt::AscendingOrder); }
void PlannerTaskFilterModel::setStatusFilter(QString value) { beginFilterChange(); m_status = std::move(value); endFilterChange(); }
void PlannerTaskFilterModel::setSearchText(QString value) { beginFilterChange(); m_search = value.trimmed().toCaseFolded(); endFilterChange(); }
void PlannerTaskFilterModel::setPriorityFilter(QString value) { beginFilterChange(); m_priority = std::move(value); endFilterChange(); }
void PlannerTaskFilterModel::setCompletionFilter(int value) { beginFilterChange(); m_completion = value; endFilterChange(); }
void PlannerTaskFilterModel::setTagFilter(QString value) { beginFilterChange(); m_tag = value.trimmed().toCaseFolded(); endFilterChange(); }
void PlannerTaskFilterModel::setDateFilter(QString value, bool undatedOnly) { beginFilterChange(); m_date = std::move(value); m_undatedOnly = undatedOnly; endFilterChange(); }
void PlannerTaskFilterModel::setSortField(int role, Qt::SortOrder order) { m_sortField = role; sort(0, order); }

bool PlannerTaskFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const auto index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!m_status.isEmpty() && index.data(PlannerTaskModel::StatusRole).toString() != m_status) return false;
    if (!m_priority.isEmpty() && index.data(PlannerTaskModel::PriorityRole).toString() != m_priority) return false;
    if (m_completion >= 0 && index.data(PlannerTaskModel::CompletedRole).toBool() != (m_completion == 1)) return false;
    const auto tags = index.data(PlannerTaskModel::TagsRole).toStringList();
    if (!m_tag.isEmpty() && std::none_of(tags.cbegin(), tags.cend(), [this](const auto& tag) { return tag.toCaseFolded().contains(m_tag); })) return false;
    const QString due = index.data(PlannerTaskModel::DueDateRole).toString();
    if (m_undatedOnly && !due.isEmpty()) return false;
    if (!m_date.isEmpty() && due != m_date) return false;
    if (!m_search.isEmpty()) {
        QStringList searchable{index.data(PlannerTaskModel::TitleRole).toString(),
            index.data(PlannerTaskModel::DescriptionRole).toString(), index.data(PlannerTaskModel::GitBranchRole).toString()};
        searchable.append(tags);
        const bool found = std::any_of(searchable.cbegin(), searchable.cend(), [this](const auto& value) { return value.toCaseFolded().contains(m_search); });
        if (!found) return false;
    }
    return true;
}

bool PlannerTaskFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const int role = m_sortField;
    if (role == PlannerTaskModel::PriorityRole) {
        const auto rank = [](const QString& value) { return value == QStringLiteral("high") ? 0 : value == QStringLiteral("medium") ? 1 : 2; };
        return rank(left.data(role).toString()) < rank(right.data(role).toString());
    }
    if (role == PlannerTaskModel::TitleRole) return QString::localeAwareCompare(left.data(role).toString(), right.data(role).toString()) < 0;
    if (role == PlannerTaskModel::DueDateRole) {
        const auto a = left.data(role).toString(), b = right.data(role).toString();
        if (a.isEmpty() != b.isEmpty()) return !a.isEmpty();
        return a < b;
    }
    return QSortFilterProxyModel::lessThan(left, right);
}

} // namespace devhub::presentation::overview::planner
