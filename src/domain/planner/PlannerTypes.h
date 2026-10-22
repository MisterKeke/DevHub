#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace devhub::domain::planner
{

enum class PlannerStatus
{
    Backlog,
    Todo,
    InProgress,
    InReview,
    Done
};
enum class PlannerSortField
{
    BoardPosition,
    CreatedAt,
    UpdatedAt,
    DueDate,
    Priority,
    Title
};

[[nodiscard]] QString plannerStatusName(PlannerStatus status);
[[nodiscard]] std::optional<PlannerStatus> plannerStatusFromName(const QString &name);

struct PlannerSubtask final
{
    QString id;
    QString sourceSubtaskId;
    QString title;
    bool completed = false;
    int position = 0;
};

struct PlannerTask final
{
    QString id;
    QString workspaceId;
    QString title;
    QString description;
    bool completed = false;
    PlannerStatus status = PlannerStatus::Todo;
    qint64 boardPosition = 0;
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
    QDate dueDate;
    QDate startDate;
    QString priority = QStringLiteral("medium");
    QString difficulty;
    qint64 revision = 1;
    std::optional<qint64> estimatedMinutes;
    qint64 actualTrackedMinutes = 0;
    QString milestoneId;
    QString gitBranch;
    QString githubIssueUrl;
    QString githubPullRequestUrl;
    QString notes;
    QString blockedReason;
    QString sourceType = QStringLiteral("local");
    QString sourceProfileIdentity;
    QString sourceTaskId;
    QString sourceRevision;
    QString sourceCreatedAt;
    QString sourceUpdatedAt;
    QDateTime importedAtUtc;
    QStringList tags;
    QVector<PlannerSubtask> subtasks;
    QStringList dependencyIds;
    QStringList prerequisiteTitles;
    QStringList dependentTaskIds;
    QStringList dependentTaskTitles;
    QStringList incompletePrerequisiteTitles;
    bool blockedByDependency = false;
};

struct PlannerMilestone final
{
    QString id;
    QString workspaceId;
    QString title;
    QString description;
    QDate startDate;
    QDate dueDate;
    bool completed = false;
    qint64 revision = 1;
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
    int taskCount = 0;
    int completedTaskCount = 0;
};

struct PlannerTimeEntry final
{
    QString id;
    QString workspaceId;
    QString taskId;
    qint64 durationMinutes = 0;
    QString note;
    QDateTime recordedAtUtc;
};

struct PlannerTaskFilter final
{
    QString searchText;
    std::optional<PlannerStatus> status;
    std::optional<bool> completed;
    std::optional<QString> priority;
    QString milestoneId;
    QString gitBranch;
    QString tag;
    QString sourceType;
    std::optional<bool> blocked;
    std::optional<bool> overdue;
    PlannerSortField sort = PlannerSortField::BoardPosition;
    bool descending = false;
    int limit = 50;
    int offset = 0;
};

} // namespace devhub::domain::planner
