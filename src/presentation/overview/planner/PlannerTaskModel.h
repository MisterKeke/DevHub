#pragma once

#include "../../../domain/planner/PlannerTypes.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>

namespace devhub::presentation::overview::planner {

class PlannerTaskModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1, TitleRole, DescriptionRole, CompletedRole, StatusRole,
        StatusLabelRole, BoardPositionRole, DueDateRole, StartDateRole, PriorityRole,
        DifficultyRole, RevisionRole, EstimatedMinutesRole, ActualTrackedMinutesRole,
        MilestoneIdRole, GitBranchRole, IssueUrlRole, PullRequestUrlRole, NotesRole,
        BlockedReasonRole, SourceTypeRole, SourceProfileIdentityRole, SourceTaskIdRole,
        SourceRevisionRole, SourceCreatedAtRole, SourceUpdatedAtRole, ImportedAtRole,
        TagsRole, SubtasksRole, DependencyIdsRole, OverdueRole, SubtaskCountRole,
        CompletedSubtaskCountRole, CreatedAtRole, UpdatedAtRole, BlockedByDependencyRole,
        IncompletePrerequisiteTitlesRole, DependentTaskIdsRole, PrerequisiteTitlesRole,
        DependentTaskTitlesRole, BlockedRole, LinkedServiceIdRole
    };
    explicit PlannerTaskModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    const QVector<domain::planner::PlannerTask>& tasks() const noexcept { return m_tasks; }
    const domain::planner::PlannerTask* taskAt(int row) const;
    const domain::planner::PlannerTask* taskById(const QString& id) const;
    void setTasks(QVector<domain::planner::PlannerTask> tasks);
    void clear();
private:
    QVector<domain::planner::PlannerTask> m_tasks;
};

class PlannerTaskFilterModel final : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit PlannerTaskFilterModel(QObject* parent = nullptr);
    void setStatusFilter(QString value);
    void setSearchText(QString value);
    void setPriorityFilter(QString value);
    void setCompletionFilter(int value);
    void setTagFilter(QString value);
    void setDateFilter(QString value, bool undatedOnly = false);
    void setSortField(int role, Qt::SortOrder order);
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
private:
    QString m_status;
    QString m_search;
    QString m_priority;
    QString m_tag;
    QString m_date;
    int m_completion = -1;
    bool m_undatedOnly = false;
    int m_sortField = PlannerTaskModel::BoardPositionRole;
};

} // namespace devhub::presentation::overview::planner
