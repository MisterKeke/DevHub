#include "PlannerValidation.h"

#include "../../domain/errors/AppError.h"

#include <QRegularExpression>
#include <QUrl>

namespace devhub::application::planner
{
namespace
{
using devhub::domain::errors::AppError;
using devhub::domain::errors::ErrorCategory;
using devhub::domain::errors::ErrorCode;
using devhub::shared::common::Result;

Result<void> invalid(const QString &message, const QString &operation)
{
    return Result<void>::failure(AppError::failure(
        ErrorCode::InvalidArgument, ErrorCategory::Workspace, message, message, operation));
}

bool dateIsValid(const QDate &date)
{
    return !date.isValid() || date.toString(Qt::ISODate).size() == 10;
}
} // namespace

Result<void> PlannerValidation::validateGitHubUrl(const QString &value) const
{
    if (value.isEmpty())
        return Result<void>::success();
    const QUrl url(value);
    if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
        url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) != 0 ||
        !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 443) ||
        !QRegularExpression(QStringLiteral("^/[^/]+/[^/]+/(issues|pull)/[1-9][0-9]*/?$"))
             .match(url.path()).hasMatch())
    {
        return invalid(QStringLiteral("Enter a valid HTTPS GitHub issue or pull request URL."),
                       QStringLiteral("validate Planner GitHub link"));
    }
    return Result<void>::success();
}

Result<void> PlannerValidation::validateGitHubIssueUrl(const QString &value) const
{
    auto valid = validateGitHubUrl(value);
    if (!valid || value.isEmpty()) return valid;
    if (!QRegularExpression(QStringLiteral("^/[^/]+/[^/]+/issues/[1-9][0-9]*/?$"))
             .match(QUrl(value).path()).hasMatch())
        return invalid(QStringLiteral("Enter a GitHub Issue URL ending in /issues/<number>."),
                       QStringLiteral("validate Planner GitHub issue link"));
    return Result<void>::success();
}

Result<void> PlannerValidation::validateGitHubPullRequestUrl(const QString &value) const
{
    auto valid = validateGitHubUrl(value);
    if (!valid || value.isEmpty()) return valid;
    if (!QRegularExpression(QStringLiteral("^/[^/]+/[^/]+/pull/[1-9][0-9]*/?$"))
             .match(QUrl(value).path()).hasMatch())
        return invalid(QStringLiteral("Enter a GitHub Pull Request URL ending in /pull/<number>."),
                       QStringLiteral("validate Planner GitHub pull request link"));
    return Result<void>::success();
}

Result<void> PlannerValidation::validateTask(const domain::planner::PlannerTask &task) const
{
    if (task.id.isEmpty() || task.workspaceId.isEmpty() || task.title.trimmed().isEmpty())
        return invalid(QStringLiteral("A Planner task needs an ID, workspace, and title."),
                       QStringLiteral("validate Planner task"));
    if (task.completed != (task.status == domain::planner::PlannerStatus::Done))
        return invalid(QStringLiteral("Task completion must match its Done status."),
                       QStringLiteral("validate Planner task"));
    if (!dateIsValid(task.dueDate) || !dateIsValid(task.startDate))
        return invalid(QStringLiteral("Enter dates using valid calendar dates."),
                       QStringLiteral("validate Planner task dates"));
    if ((task.estimatedMinutes && *task.estimatedMinutes < 0) || task.actualTrackedMinutes < 0)
        return invalid(QStringLiteral("Tracked and estimated minutes cannot be negative."),
                       QStringLiteral("validate Planner task time"));
    if (task.priority != QStringLiteral("low") && task.priority != QStringLiteral("medium") &&
        task.priority != QStringLiteral("high"))
        return invalid(QStringLiteral("Choose low, medium, or high task priority."),
                       QStringLiteral("validate Planner task priority"));
    if (!task.difficulty.isEmpty() && task.difficulty != QStringLiteral("easy") &&
        task.difficulty != QStringLiteral("medium") && task.difficulty != QStringLiteral("hard"))
        return invalid(QStringLiteral("Choose easy, medium, hard, or no difficulty."),
                       QStringLiteral("validate Planner task difficulty"));
    if (task.revision < 1 || task.boardPosition < 0)
        return invalid(QStringLiteral("Task revision and board position must be positive values."),
                       QStringLiteral("validate Planner task ordering"));
    if (task.sourceType != QStringLiteral("local") &&
        task.sourceType != QStringLiteral("somethingqt"))
        return invalid(QStringLiteral("Choose a supported Planner task source."),
                       QStringLiteral("validate Planner task source"));
    if (task.sourceType == QStringLiteral("somethingqt") &&
        (task.sourceProfileIdentity.trimmed().isEmpty() || task.sourceTaskId.isEmpty()))
        return invalid(
            QStringLiteral("Imported tasks need their original profile and task identities."),
            QStringLiteral("validate Planner task provenance"));
    for (const domain::planner::PlannerSubtask &subtask : task.subtasks)
    {
        if (subtask.title.trimmed().isEmpty() || subtask.position < 0)
            return invalid(
                QStringLiteral("Each subtask needs a title and a non-negative position."),
                QStringLiteral("validate Planner subtasks"));
    }
    if (!task.gitBranch.contains(QChar::Null) && !task.gitBranch.contains(QLatin1Char('\n')) &&
        !task.gitBranch.contains(QLatin1Char('\r')))
    {
        // The branch name is opaque metadata here; Git-specific validation belongs to Git
        // operations.
    }
    else
    {
        return invalid(QStringLiteral("The Git branch name contains an invalid control character."),
                       QStringLiteral("validate Planner branch"));
    }
    auto issue = validateGitHubIssueUrl(task.githubIssueUrl);
    if (!issue)
        return issue;
    return validateGitHubPullRequestUrl(task.githubPullRequestUrl);
}

Result<void>
PlannerValidation::validateMilestone(const domain::planner::PlannerMilestone &milestone) const
{
    if (milestone.id.isEmpty() || milestone.workspaceId.isEmpty() ||
        milestone.title.trimmed().isEmpty())
        return invalid(QStringLiteral("A milestone needs an ID, workspace, and title."),
                       QStringLiteral("validate Planner milestone"));
    if (!dateIsValid(milestone.startDate) || !dateIsValid(milestone.dueDate) ||
        milestone.revision < 1)
        return invalid(QStringLiteral("Enter valid milestone dates and revision."),
                       QStringLiteral("validate Planner milestone"));
    return Result<void>::success();
}

Result<void> PlannerValidation::validateTimeEntry(const domain::planner::PlannerTimeEntry &entry) const
{
    if (entry.workspaceId.isEmpty() || entry.taskId.isEmpty() || entry.durationMinutes <= 0)
        return invalid(QStringLiteral("A time entry needs a task and a duration greater than zero minutes."),
                       QStringLiteral("validate Planner time entry"));
    return Result<void>::success();
}

Result<void>
PlannerValidation::validateFilter(const domain::planner::PlannerTaskFilter &filter) const
{
    if (filter.limit < 1 || filter.limit > 200 || filter.offset < 0)
        return invalid(
            QStringLiteral(
                "Planner list pages must contain 1 to 200 tasks and use a non-negative offset."),
            QStringLiteral("validate Planner task page"));
    if (!filter.sourceType.isEmpty() && filter.sourceType != QStringLiteral("local")
        && filter.sourceType != QStringLiteral("somethingqt"))
        return invalid(QStringLiteral("Choose local or imported task origin."),
                       QStringLiteral("validate Planner task origin filter"));
    return Result<void>::success();
}

} // namespace devhub::application::planner
