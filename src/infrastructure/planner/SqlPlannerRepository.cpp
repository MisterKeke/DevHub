#include "SqlPlannerRepository.h"

#include "../../domain/planner/PlannerRules.h"
#include "../database/SqlErrorMapper.h"
#include "../database/SqlTransactionGuard.h"

#include <QSet>
#include <QDate>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>

#include <limits>

namespace devhub::infrastructure::planner
{
using domain::errors::AppError;
using domain::errors::ErrorCategory;
using domain::errors::ErrorCode;
using domain::planner::PlannerMilestone;
using domain::planner::PlannerActionRun;
using domain::planner::PlannerStatus;
using domain::planner::PlannerSubtask;
using domain::planner::PlannerTask;
using domain::planner::PlannerTaskFilter;
using shared::common::Result;

namespace
{

template <typename T> Result<T> missing(const QString &operation)
{
    return Result<T>::failure(AppError::failure(
        ErrorCode::InvalidArgument, ErrorCategory::Workspace,
        QStringLiteral("The requested Planner record was not found in this workspace."),
        QStringLiteral("The record did not match the requested workspace, account, or revision."),
        operation));
}

template <typename T> Result<T> threadFailure(const QString &operation)
{
    return Result<T>::failure(AppError::failure(
        ErrorCode::DatabaseQueryFailed, ErrorCategory::Database,
        QStringLiteral("Planner data is unavailable right now."),
        QStringLiteral("Qt SQL access was requested outside the database manager's owning thread."),
        operation));
}

template <typename T> Result<T> sqlFailure(const QSqlQuery &query, const QString &operation)
{
    return Result<T>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), operation));
}

template <typename T> Result<T> conflict(const QString &operation)
{
    return Result<T>::failure(AppError::failure(
        ErrorCode::RevisionConflict, ErrorCategory::Persistence,
        QStringLiteral("This Planner record changed since it was opened. Reload it and try again."),
        QStringLiteral("Optimistic revision check failed for %1.").arg(operation), operation));
}

Result<void> advanceTaskRevision(QSqlDatabase database, const domain::account::AccountId &accountId,
                                 const QString &workspaceId, const QString &taskId,
                                 qint64 expectedRevision, const QString &operation)
{
    QSqlQuery query(database);
    query.prepare(
        QStringLiteral("UPDATE planner_tasks SET "
                       "revision=revision+1,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                       "WHERE id=? AND workspace_id=? AND revision=? AND workspace_id IN(SELECT id "
                       "FROM workspaces WHERE account_id=?)"));
    query.addBindValue(taskId);
    query.addBindValue(workspaceId);
    query.addBindValue(expectedRevision);
    query.addBindValue(accountId.toString());
    if (!query.exec())
        return sqlFailure<void>(query, operation);
    return query.numRowsAffected() == 1 ? Result<void>::success() : conflict<void>(operation);
}

QVariant dateValue(const QDate &date)
{
    return date.isValid() ? QVariant(date.toString(Qt::ISODate)) : QVariant();
}

QDate readDate(const QVariant &value)
{
    return value.isNull() ? QDate() : QDate::fromString(value.toString(), Qt::ISODate);
}

QString timestamp(const QDateTime &value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

QString textValue(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QDateTime readTimestamp(const QVariant &value)
{
    QDateTime parsed = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!parsed.isValid())
        parsed = QDateTime::fromString(value.toString(), Qt::ISODate);
    return parsed.toUTC();
}

bool uniqueIds(const QStringList &ids)
{
    QSet<QString> unique;
    for (const QString &id : ids)
    {
        if (id.isEmpty() || unique.contains(id))
            return false;
        unique.insert(id);
    }
    return true;
}

QVariant optionalMinutes(const std::optional<qint64> &minutes)
{
    return minutes ? QVariant(*minutes) : QVariant();
}

} // namespace

SqlPlannerRepository::SqlPlannerRepository(database::InternalDatabaseManager &database,
                                           domain::account::AccountId accountId)
    : m_database(database), m_accountId(std::move(accountId))
{
}

bool SqlPlannerRepository::onOwnerThread() const
{
    return QThread::currentThread() == m_database.thread();
}

Result<PlannerTask> SqlPlannerRepository::createTask(PlannerTask task)
{
    return createTaskImpl(std::move(task), false);
}

Result<PlannerTask> SqlPlannerRepository::createImportedTask(PlannerTask task,
                                                              bool allowAdditionalCopy)
{
    if (task.sourceType != QStringLiteral("somethingqt"))
        return Result<PlannerTask>::failure(AppError::failure(
            ErrorCode::InvalidArgument, ErrorCategory::Workspace,
            QStringLiteral("Only SomethingQt tasks can be imported."),
            QStringLiteral("The imported task source type was not somethingqt."),
            QStringLiteral("create Planner import")));
    return createTaskImpl(std::move(task), allowAdditionalCopy);
}

Result<QHash<QString, int>> SqlPlannerRepository::importedSourceTaskCounts(
    const QString& workspaceId, const QString& sourceProfileIdentity)
{
    if (!onOwnerThread())
        return threadFailure<QHash<QString, int>>(QStringLiteral("read Planner import records"));
    QHash<QString, int> counts;
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "SELECT i.source_task_id,COUNT(*) FROM planner_import_records i JOIN workspaces w "
        "ON w.id=i.workspace_id WHERE i.workspace_id=? AND w.account_id=? AND w.archived_at_utc IS NULL "
        "AND i.source_type='somethingqt' AND i.source_profile_identity=? GROUP BY i.source_task_id"));
    query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString());
    query.addBindValue(sourceProfileIdentity);
    if (!query.exec())
        return sqlFailure<QHash<QString, int>>(query, QStringLiteral("read Planner import records"));
    while (query.next()) counts.insert(query.value(0).toString(), query.value(1).toInt());
    return Result<QHash<QString, int>>::success(std::move(counts));
}

Result<PlannerTask> SqlPlannerRepository::createTaskImpl(PlannerTask task, bool allowAdditionalCopy)
{
    if (!onOwnerThread())
        return threadFailure<PlannerTask>(QStringLiteral("create Planner task"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<PlannerTask>::failure(*transaction.error());
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "INSERT INTO "
        "planner_tasks(id,workspace_id,title,description,is_completed,status,board_position,"
        "created_at_utc,updated_at_utc,"
        "due_date,start_date,priority,difficulty,revision,estimated_minutes,actual_tracked_minutes,"
        "milestone_id,git_branch,"
        "github_issue_url,github_pull_request_url,planner_notes,blocked_reason,source_type,linked_service_id) "
        "SELECT ?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,? WHERE EXISTS("
        "SELECT 1 FROM workspaces WHERE id=? AND account_id=? AND archived_at_utc IS NULL)"));
    query.addBindValue(task.id);
    query.addBindValue(task.workspaceId);
    query.addBindValue(task.title);
    query.addBindValue(textValue(task.description));
    query.addBindValue(task.completed ? 1 : 0);
    query.addBindValue(domain::planner::plannerStatusName(task.status));
    query.addBindValue(task.boardPosition);
    query.addBindValue(timestamp(task.createdAtUtc));
    query.addBindValue(timestamp(task.updatedAtUtc));
    query.addBindValue(dateValue(task.dueDate));
    query.addBindValue(dateValue(task.startDate));
    query.addBindValue(task.priority);
    query.addBindValue(textValue(task.difficulty));
    query.addBindValue(task.revision);
    query.addBindValue(optionalMinutes(task.estimatedMinutes));
    query.addBindValue(task.actualTrackedMinutes);
    query.addBindValue(task.milestoneId.isEmpty() ? QVariant() : QVariant(task.milestoneId));
    query.addBindValue(textValue(task.gitBranch));
    query.addBindValue(textValue(task.githubIssueUrl));
    query.addBindValue(textValue(task.githubPullRequestUrl));
    query.addBindValue(textValue(task.notes));
    query.addBindValue(textValue(task.blockedReason));
    query.addBindValue(textValue(task.sourceType));
    query.addBindValue(textValue(task.linkedServiceId));
    query.addBindValue(task.workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<PlannerTask>(query, QStringLiteral("create Planner task"));
    if (query.numRowsAffected() != 1)
        return missing<PlannerTask>(QStringLiteral("create Planner task"));
    query.finish();

    if (task.sourceType == QStringLiteral("somethingqt"))
    {
        QSqlQuery existing(m_database.database());
        existing.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(copy_number),0) FROM planner_import_records WHERE workspace_id=? "
            "AND source_type='somethingqt' AND source_profile_identity=? AND source_task_id=?"));
        existing.addBindValue(task.workspaceId);
        existing.addBindValue(task.sourceProfileIdentity);
        existing.addBindValue(task.sourceTaskId);
        if (!existing.exec() || !existing.next())
            return sqlFailure<PlannerTask>(existing, QStringLiteral("check Planner import duplicates"));
        const int previousCopy = existing.value(0).toInt();
        if (previousCopy > 0 && !allowAdditionalCopy)
            return Result<PlannerTask>::failure(AppError::failure(
                ErrorCode::InvalidArgument, ErrorCategory::Workspace,
                QStringLiteral("This SomethingQt task has already been imported into this workspace."),
                QStringLiteral("Default import policy skips a source task already recorded for this workspace."),
                QStringLiteral("create Planner import")));
        const int copyNumber = previousCopy + 1;
        QSqlQuery provenance(m_database.database());
        provenance.prepare(
            QStringLiteral("INSERT INTO "
                           "planner_import_records(task_id,workspace_id,source_type,source_profile_"
                           "identity,source_task_id,"
                           "source_revision,source_created_at,source_updated_at,imported_at_utc,copy_number) "
                           "VALUES(?,?,'somethingqt',?,?,?,?,?,?,?)"));
        provenance.addBindValue(task.id);
        provenance.addBindValue(task.workspaceId);
        provenance.addBindValue(task.sourceProfileIdentity);
        provenance.addBindValue(task.sourceTaskId);
        provenance.addBindValue(task.sourceRevision.isEmpty() ? QVariant()
                                                              : QVariant(task.sourceRevision));
        provenance.addBindValue(task.sourceCreatedAt.isEmpty() ? QVariant()
                                                               : QVariant(task.sourceCreatedAt));
        provenance.addBindValue(task.sourceUpdatedAt.isEmpty() ? QVariant()
                                                               : QVariant(task.sourceUpdatedAt));
        provenance.addBindValue(timestamp(task.importedAtUtc));
        provenance.addBindValue(copyNumber);
        if (!provenance.exec())
            return sqlFailure<PlannerTask>(provenance,
                                           QStringLiteral("record Planner import provenance"));
    }
    QSet<QString> tagNames;
    for (const QString &original : task.tags)
    {
        const QString name = original.trimmed();
        const QString normalized = name.toCaseFolded();
        if (name.isEmpty() || tagNames.contains(normalized))
            continue;
        tagNames.insert(normalized);
        QSqlQuery tag(m_database.database());
        tag.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO planner_tags(id,workspace_id,name,name_normalized) SELECT "
            "?,?,?,? WHERE EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=?)"));
        tag.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
        tag.addBindValue(task.workspaceId);
        tag.addBindValue(name);
        tag.addBindValue(normalized);
        tag.addBindValue(task.workspaceId);
        tag.addBindValue(m_accountId.toString());
        if (!tag.exec())
            return sqlFailure<PlannerTask>(tag, QStringLiteral("create Planner task tag"));
        tag.finish();
        QSqlQuery link(m_database.database());
        link.prepare(
            QStringLiteral("INSERT INTO planner_task_tags(workspace_id,task_id,tag_id) SELECT "
                           "?,?,id FROM planner_tags WHERE workspace_id=? AND name_normalized=?"));
        link.addBindValue(task.workspaceId);
        link.addBindValue(task.id);
        link.addBindValue(task.workspaceId);
        link.addBindValue(normalized);
        if (!link.exec())
            return sqlFailure<PlannerTask>(link, QStringLiteral("link Planner task tag"));
    }
    for (const PlannerSubtask &subtask : task.subtasks)
    {
        QSqlQuery insertSubtask(m_database.database());
        insertSubtask.prepare(QStringLiteral(
            "INSERT INTO "
            "planner_subtasks(id,workspace_id,task_id,source_subtask_id,title,is_completed,"
            "position,created_at_utc,updated_at_utc) VALUES(?,?,?,?,?,?,?,?,?)"));
        insertSubtask.addBindValue(subtask.id);
        insertSubtask.addBindValue(task.workspaceId);
        insertSubtask.addBindValue(task.id);
        insertSubtask.addBindValue(
            subtask.sourceSubtaskId.isEmpty() ? QVariant() : QVariant(subtask.sourceSubtaskId));
        insertSubtask.addBindValue(subtask.title);
        insertSubtask.addBindValue(subtask.completed ? 1 : 0);
        insertSubtask.addBindValue(subtask.position);
        insertSubtask.addBindValue(timestamp(task.createdAtUtc));
        insertSubtask.addBindValue(timestamp(task.updatedAtUtc));
        if (!insertSubtask.exec())
            return sqlFailure<PlannerTask>(insertSubtask,
                                           QStringLiteral("create Planner task subtask"));
    }
    auto committed = transaction.commit();
    if (!committed)
        return Result<PlannerTask>::failure(committed.error());
    return getTask(task.workspaceId, task.id);
}

Result<PlannerTask> SqlPlannerRepository::getTask(const QString &workspaceId, const QString &taskId)
{
    if (!onOwnerThread())
        return threadFailure<PlannerTask>(QStringLiteral("read Planner task"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "SELECT "
        "t.id,t.workspace_id,t.title,t.description,t.is_completed,t.status,t.board_position,t."
        "created_at_utc,t.updated_at_utc,"
        "t.due_date,t.start_date,t.priority,t.difficulty,t.revision,t.estimated_minutes,t.actual_"
        "tracked_minutes,"
        "COALESCE(t.milestone_id,''),COALESCE(t.git_branch,''),COALESCE(t.github_issue_url,''),"
        "COALESCE(t.github_pull_request_url,''),"
        "t.planner_notes,t.blocked_reason,t.source_type,COALESCE(i.source_profile_identity,''),"
        "COALESCE(i.source_task_id,''),"
        "COALESCE(i.source_revision,''),i.source_created_at,i.source_updated_at,i.imported_at_utc,"
        "COALESCE(t.linked_service_id,'') "
        "FROM planner_tasks t JOIN workspaces w ON w.id=t.workspace_id LEFT JOIN "
        "planner_import_records i ON i.task_id=t.id "
        "WHERE t.workspace_id=? AND t.id=? AND w.account_id=?"));
    query.addBindValue(workspaceId);
    query.addBindValue(taskId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<PlannerTask>(query, QStringLiteral("read Planner task"));
    if (!query.next())
        return missing<PlannerTask>(QStringLiteral("read Planner task"));
    PlannerTask task;
    task.id = query.value(0).toString();
    task.workspaceId = query.value(1).toString();
    task.title = query.value(2).toString();
    task.description = query.value(3).toString();
    task.completed = query.value(4).toBool();
    task.status = domain::planner::plannerStatusFromName(query.value(5).toString())
                      .value_or(PlannerStatus::Todo);
    task.boardPosition = query.value(6).toLongLong();
    task.createdAtUtc = readTimestamp(query.value(7));
    task.updatedAtUtc = readTimestamp(query.value(8));
    task.dueDate = readDate(query.value(9));
    task.startDate = readDate(query.value(10));
    task.priority = query.value(11).toString();
    task.difficulty = query.value(12).toString();
    task.revision = query.value(13).toLongLong();
    if (!query.value(14).isNull())
        task.estimatedMinutes = query.value(14).toLongLong();
    task.actualTrackedMinutes = query.value(15).toLongLong();
    task.milestoneId = query.value(16).toString();
    task.gitBranch = query.value(17).toString();
    task.githubIssueUrl = query.value(18).toString();
    task.githubPullRequestUrl = query.value(19).toString();
    task.notes = query.value(20).toString();
    task.blockedReason = query.value(21).toString();
    task.sourceType = query.value(22).toString();
    task.sourceProfileIdentity = query.value(23).toString();
    task.sourceTaskId = query.value(24).toString();
    task.sourceRevision = query.value(25).toString();
    task.sourceCreatedAt = query.value(26).toString();
    task.sourceUpdatedAt = query.value(27).toString();
    if (!query.value(28).isNull())
        task.importedAtUtc = readTimestamp(query.value(28));
    task.linkedServiceId = query.value(29).toString();
    query.finish();

    QSqlQuery details(m_database.database());
    details.prepare(QStringLiteral(
        "SELECT t.name FROM planner_task_tags l JOIN planner_tags t ON t.id=l.tag_id "
        "WHERE l.workspace_id=? AND l.task_id=? ORDER BY t.name COLLATE NOCASE"));
    details.addBindValue(workspaceId);
    details.addBindValue(taskId);
    if (!details.exec())
        return sqlFailure<PlannerTask>(details, QStringLiteral("read Planner task tags"));
    while (details.next())
        task.tags.append(details.value(0).toString());
    details.finish();
    details.prepare(
        QStringLiteral("SELECT id,COALESCE(source_subtask_id,''),title,is_completed,position FROM "
                       "planner_subtasks "
                       "WHERE workspace_id=? AND task_id=? ORDER BY position,id"));
    details.addBindValue(workspaceId);
    details.addBindValue(taskId);
    if (!details.exec())
        return sqlFailure<PlannerTask>(details, QStringLiteral("read Planner subtasks"));
    while (details.next())
        task.subtasks.append({details.value(0).toString(), details.value(1).toString(),
                              details.value(2).toString(), details.value(3).toBool(),
                              details.value(4).toInt()});
    details.finish();
    details.prepare(QStringLiteral("SELECT depends_on_task_id FROM planner_task_dependencies WHERE "
                                   "workspace_id=? AND task_id=? ORDER BY depends_on_task_id"));
    details.addBindValue(workspaceId);
    details.addBindValue(taskId);
    if (!details.exec())
        return sqlFailure<PlannerTask>(details, QStringLiteral("read Planner task dependencies"));
    while (details.next())
        task.dependencyIds.append(details.value(0).toString());
    details.finish();
    details.prepare(QStringLiteral(
        "SELECT p.id,p.title,p.is_completed FROM planner_task_dependencies d JOIN planner_tasks p "
        "ON p.workspace_id=d.workspace_id AND p.id=d.depends_on_task_id "
        "WHERE d.workspace_id=? AND d.task_id=? ORDER BY p.title COLLATE NOCASE,p.id"));
    details.addBindValue(workspaceId);
    details.addBindValue(taskId);
    if (!details.exec())
        return sqlFailure<PlannerTask>(details, QStringLiteral("read Planner task prerequisites"));
    while (details.next()) {
        task.prerequisiteTitles.append(details.value(1).toString());
        if (!details.value(2).toBool()) {
            task.blockedByDependency = true;
            task.incompletePrerequisiteTitles.append(details.value(1).toString());
        }
    }
    details.finish();
    details.prepare(QStringLiteral(
        "SELECT t.id,t.title FROM planner_task_dependencies d JOIN planner_tasks t "
        "ON t.workspace_id=d.workspace_id AND t.id=d.task_id "
        "WHERE d.workspace_id=? AND d.depends_on_task_id=? ORDER BY t.title COLLATE NOCASE,t.id"));
    details.addBindValue(workspaceId);
    details.addBindValue(taskId);
    if (!details.exec())
        return sqlFailure<PlannerTask>(details, QStringLiteral("read Planner dependent tasks"));
    while (details.next()) {
        task.dependentTaskIds.append(details.value(0).toString());
        task.dependentTaskTitles.append(details.value(1).toString());
    }
    return Result<PlannerTask>::success(std::move(task));
}

Result<QVector<PlannerTask>> SqlPlannerRepository::listTasks(const QString &workspaceId,
                                                             const PlannerTaskFilter &filter)
{
    if (!onOwnerThread())
        return threadFailure<QVector<PlannerTask>>(QStringLiteral("list Planner tasks"));
    QString sql = QStringLiteral("SELECT t.id FROM planner_tasks t JOIN workspaces w ON "
                                 "w.id=t.workspace_id WHERE t.workspace_id=? AND w.account_id=?");
    if (!filter.searchText.isEmpty())
        sql += QStringLiteral(
            " AND (t.title LIKE ? ESCAPE '!' OR t.description LIKE ? ESCAPE '!' OR t.git_branch LIKE ? ESCAPE '!' "
            "OR EXISTS(SELECT 1 FROM planner_task_tags sl JOIN planner_tags sg ON sg.id=sl.tag_id "
            "WHERE sl.workspace_id=t.workspace_id AND sl.task_id=t.id AND sg.name LIKE ? ESCAPE '!'))");
    if (filter.status)
        sql += QStringLiteral(" AND t.status=?");
    if (filter.completed)
        sql += QStringLiteral(" AND t.is_completed=?");
    if (filter.priority)
        sql += QStringLiteral(" AND t.priority=?");
    if (!filter.milestoneId.isEmpty())
        sql += QStringLiteral(" AND t.milestone_id=?");
    if (!filter.gitBranch.isEmpty())
        sql += QStringLiteral(" AND t.git_branch=?");
    if (!filter.tag.isEmpty())
        sql += QStringLiteral(
            " AND EXISTS(SELECT 1 FROM planner_task_tags l JOIN planner_tags g ON g.id=l.tag_id "
            "WHERE l.workspace_id=t.workspace_id AND l.task_id=t.id AND g.name_normalized LIKE ? ESCAPE '!')");
    if (!filter.sourceType.isEmpty())
        sql += QStringLiteral(" AND t.source_type=?");
    const QString blockedExpression = QStringLiteral(
        "(t.blocked_reason<>'' OR EXISTS(SELECT 1 FROM planner_task_dependencies d "
        "JOIN planner_tasks p ON p.workspace_id=d.workspace_id AND p.id=d.depends_on_task_id "
        "WHERE d.workspace_id=t.workspace_id AND d.task_id=t.id AND p.is_completed=0))");
    if (filter.blocked)
        sql += *filter.blocked ? QStringLiteral(" AND %1").arg(blockedExpression)
                               : QStringLiteral(" AND NOT %1").arg(blockedExpression);
    if (filter.overdue)
        sql += *filter.overdue ? QStringLiteral(" AND t.is_completed=0 AND t.due_date IS NOT NULL AND t.due_date<?")
                               : QStringLiteral(" AND (t.is_completed=1 OR t.due_date IS NULL OR t.due_date>=?)");
    QString order;
    switch (filter.sort)
    {
    case domain::planner::PlannerSortField::BoardPosition:
        order = QStringLiteral("t.status,t.board_position,t.created_at_utc,t.id");
        break;
    case domain::planner::PlannerSortField::CreatedAt:
        order = QStringLiteral("t.created_at_utc,t.id");
        break;
    case domain::planner::PlannerSortField::UpdatedAt:
        order = QStringLiteral("t.updated_at_utc,t.id");
        break;
    case domain::planner::PlannerSortField::DueDate:
        order = QStringLiteral("t.due_date,t.id");
        break;
    case domain::planner::PlannerSortField::Priority:
        order = QStringLiteral("CASE t.priority WHEN 'high' THEN 0 WHEN 'medium' THEN 1 ELSE 2 "
                               "END,t.created_at_utc,t.id");
        break;
    case domain::planner::PlannerSortField::Title:
        order = QStringLiteral("t.title COLLATE NOCASE,t.id");
        break;
    }
    sql += QStringLiteral(" ORDER BY %1 %2 LIMIT ? OFFSET ?")
               .arg(order, filter.descending ? QStringLiteral("DESC") : QStringLiteral("ASC"));
    QSqlQuery query(m_database.database());
    query.prepare(sql);
    query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!filter.searchText.isEmpty())
    {
        QString escaped = filter.searchText;
        escaped.replace(QLatin1Char('!'), QStringLiteral("!!"));
        escaped.replace(QLatin1Char('%'), QStringLiteral("!%"));
        escaped.replace(QLatin1Char('_'), QStringLiteral("!_"));
        const QString term = QStringLiteral("%") + escaped + QStringLiteral("%");
        query.addBindValue(term);
        query.addBindValue(term);
        query.addBindValue(term);
        query.addBindValue(term);
    }
    if (filter.status)
        query.addBindValue(domain::planner::plannerStatusName(*filter.status));
    if (filter.completed)
        query.addBindValue(*filter.completed ? 1 : 0);
    if (filter.priority)
        query.addBindValue(*filter.priority);
    if (!filter.milestoneId.isEmpty())
        query.addBindValue(filter.milestoneId);
    if (!filter.gitBranch.isEmpty())
        query.addBindValue(filter.gitBranch);
    if (!filter.tag.isEmpty()) {
        QString escaped = filter.tag.trimmed().toCaseFolded();
        escaped.replace(QLatin1Char('!'), QStringLiteral("!!"));
        escaped.replace(QLatin1Char('%'), QStringLiteral("!%"));
        escaped.replace(QLatin1Char('_'), QStringLiteral("!_"));
        query.addBindValue(QStringLiteral("%") + escaped + QStringLiteral("%"));
    }
    if (!filter.sourceType.isEmpty())
        query.addBindValue(filter.sourceType);
    if (filter.overdue)
        query.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    query.addBindValue(filter.limit);
    query.addBindValue(filter.offset);
    if (!query.exec())
        return sqlFailure<QVector<PlannerTask>>(query, QStringLiteral("list Planner tasks"));
    QStringList ids;
    while (query.next())
        ids.append(query.value(0).toString());
    query.finish();
    QVector<PlannerTask> tasks;
    tasks.reserve(ids.size());
    for (const QString &id : ids)
    {
        auto task = getTask(workspaceId, id);
        if (!task)
            return Result<QVector<PlannerTask>>::failure(task.error());
        tasks.append(std::move(task).value());
    }
    return Result<QVector<PlannerTask>>::success(std::move(tasks));
}

Result<PlannerTask> SqlPlannerRepository::updateTask(PlannerTask task, qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<PlannerTask>(QStringLiteral("update Planner task"));
    QSqlQuery query(m_database.database());
    query.prepare(
        QStringLiteral("UPDATE planner_tasks SET "
                       "title=?,description=?,is_completed=?,status=?,board_position=?,updated_at_"
                       "utc=?,due_date=?,start_date=?,"
                       "priority=?,difficulty=?,revision=revision+1,estimated_minutes=?,actual_"
                       "tracked_minutes=?,milestone_id=?,git_branch=?,"
                       "github_issue_url=?,github_pull_request_url=?,planner_notes=?,blocked_"
                       "reason=?,linked_service_id=? WHERE id=? AND workspace_id=? AND revision=? "
                       "AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(task.title);
    query.addBindValue(textValue(task.description));
    query.addBindValue(task.completed ? 1 : 0);
    query.addBindValue(domain::planner::plannerStatusName(task.status));
    query.addBindValue(task.boardPosition);
    query.addBindValue(timestamp(task.updatedAtUtc));
    query.addBindValue(dateValue(task.dueDate));
    query.addBindValue(dateValue(task.startDate));
    query.addBindValue(task.priority);
    query.addBindValue(textValue(task.difficulty));
    query.addBindValue(optionalMinutes(task.estimatedMinutes));
    query.addBindValue(task.actualTrackedMinutes);
    query.addBindValue(task.milestoneId.isEmpty() ? QVariant() : QVariant(task.milestoneId));
    query.addBindValue(textValue(task.gitBranch));
    query.addBindValue(textValue(task.githubIssueUrl));
    query.addBindValue(textValue(task.githubPullRequestUrl));
    query.addBindValue(textValue(task.notes));
    query.addBindValue(textValue(task.blockedReason));
    query.addBindValue(textValue(task.linkedServiceId));
    query.addBindValue(task.id);
    query.addBindValue(task.workspaceId);
    query.addBindValue(expectedRevision);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<PlannerTask>(query, QStringLiteral("update Planner task"));
    if (query.numRowsAffected() != 1)
        return conflict<PlannerTask>(QStringLiteral("update Planner task"));
    return getTask(task.workspaceId, task.id);
}

Result<void> SqlPlannerRepository::removeTask(const QString &workspaceId, const QString &taskId,
                                              qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<void>(QStringLiteral("delete Planner task"));
    QSqlQuery query(m_database.database());
    query.prepare(
        QStringLiteral("DELETE FROM planner_tasks WHERE id=? AND workspace_id=? AND revision=? AND "
                       "workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(taskId);
    query.addBindValue(workspaceId);
    query.addBindValue(expectedRevision);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<void>(query, QStringLiteral("delete Planner task"));
    return query.numRowsAffected() == 1 ? Result<void>::success()
                                        : conflict<void>(QStringLiteral("delete Planner task"));
}

Result<void> SqlPlannerRepository::reorderTasks(const QString &workspaceId,
                                                const QStringList &taskIds,
                                                const QVector<qint64> &expectedRevisions,
                                                PlannerStatus status)
{
    if (!onOwnerThread())
        return threadFailure<void>(QStringLiteral("reorder Planner tasks"));
    if (!uniqueIds(taskIds) || taskIds.size() != expectedRevisions.size())
        return missing<void>(QStringLiteral("reorder Planner tasks"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<void>::failure(*transaction.error());
    QSqlQuery count(m_database.database());
    count.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM planner_tasks t JOIN workspaces w ON w.id=t.workspace_id WHERE "
        "t.workspace_id=? AND t.status=? AND w.account_id=?"));
    count.addBindValue(workspaceId);
    count.addBindValue(domain::planner::plannerStatusName(status));
    count.addBindValue(m_accountId.toString());
    if (!count.exec() || !count.next())
        return sqlFailure<void>(count, QStringLiteral("validate Planner board order"));
    if (count.value(0).toInt() != taskIds.size())
        return missing<void>(QStringLiteral("reorder Planner tasks"));
    count.finish();
    QSqlQuery update(m_database.database());
    update.prepare(
        QStringLiteral("UPDATE planner_tasks SET "
                       "board_position=?,revision=revision+1,updated_at_utc=strftime('%Y-%m-%dT%H:%"
                       "M:%fZ','now') WHERE id=? AND workspace_id=? AND status=? AND revision=? "
                       "AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    for (qsizetype i = 0; i < taskIds.size(); ++i)
    {
        update.bindValue(0, static_cast<qint64>(i));
        update.bindValue(1, taskIds.at(i));
        update.bindValue(2, workspaceId);
        update.bindValue(3, domain::planner::plannerStatusName(status));
        update.bindValue(4, expectedRevisions.at(i));
        update.bindValue(5, m_accountId.toString());
        if (!update.exec())
            return sqlFailure<void>(update, QStringLiteral("reorder Planner tasks"));
        if (update.numRowsAffected() != 1)
            return conflict<void>(QStringLiteral("reorder Planner tasks"));
        update.finish();
    }
    return transaction.commit();
}

Result<void> SqlPlannerRepository::setTags(const QString &workspaceId, const QString &taskId,
                                           const QStringList &tags, qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<void>(QStringLiteral("set Planner task tags"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<void>::failure(*transaction.error());
    QSqlQuery exists(m_database.database());
    exists.prepare(
        QStringLiteral("SELECT 1 FROM planner_tasks t JOIN workspaces w ON w.id=t.workspace_id "
                       "WHERE t.id=? AND t.workspace_id=? AND w.account_id=?"));
    exists.addBindValue(taskId);
    exists.addBindValue(workspaceId);
    exists.addBindValue(m_accountId.toString());
    if (!exists.exec())
        return sqlFailure<void>(exists, QStringLiteral("validate Planner task tags"));
    if (!exists.next())
        return missing<void>(QStringLiteral("set Planner task tags"));
    exists.finish();
    auto advanced = advanceTaskRevision(m_database.database(), m_accountId, workspaceId, taskId,
                                        expectedRevision, QStringLiteral("set Planner task tags"));
    if (!advanced)
        return advanced;
    QSqlQuery clear(m_database.database());
    clear.prepare(
        QStringLiteral("DELETE FROM planner_task_tags WHERE workspace_id=? AND task_id=?"));
    clear.addBindValue(workspaceId);
    clear.addBindValue(taskId);
    if (!clear.exec())
        return sqlFailure<void>(clear, QStringLiteral("clear Planner task tags"));
    QSet<QString> seen;
    for (const QString &original : tags)
    {
        const QString name = original.trimmed();
        const QString normalized = name.toCaseFolded();
        if (name.isEmpty() || seen.contains(normalized))
            continue;
        seen.insert(normalized);
        QSqlQuery insertTag(m_database.database());
        insertTag.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO planner_tags(id,workspace_id,name,name_normalized) SELECT "
            "?,?,?,? WHERE EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=?)"));
        insertTag.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
        insertTag.addBindValue(workspaceId);
        insertTag.addBindValue(name);
        insertTag.addBindValue(normalized);
        insertTag.addBindValue(workspaceId);
        insertTag.addBindValue(m_accountId.toString());
        if (!insertTag.exec())
            return sqlFailure<void>(insertTag, QStringLiteral("create Planner tag"));
        insertTag.finish();
        QSqlQuery link(m_database.database());
        link.prepare(
            QStringLiteral("INSERT INTO planner_task_tags(workspace_id,task_id,tag_id) SELECT "
                           "?,?,id FROM planner_tags WHERE workspace_id=? AND name_normalized=?"));
        link.addBindValue(workspaceId);
        link.addBindValue(taskId);
        link.addBindValue(workspaceId);
        link.addBindValue(normalized);
        if (!link.exec())
            return sqlFailure<void>(link, QStringLiteral("link Planner tag"));
    }
    return transaction.commit();
}

Result<PlannerSubtask> SqlPlannerRepository::addSubtask(const QString &workspaceId,
                                                        const QString &taskId,
                                                        PlannerSubtask subtask,
                                                        qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<PlannerSubtask>(QStringLiteral("add Planner subtask"));
    if (subtask.title.trimmed().isEmpty() || subtask.position < 0)
        return missing<PlannerSubtask>(QStringLiteral("add Planner subtask"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<PlannerSubtask>::failure(*transaction.error());
    auto advanced = advanceTaskRevision(m_database.database(), m_accountId, workspaceId, taskId,
                                        expectedRevision, QStringLiteral("add Planner subtask"));
    if (!advanced)
        return Result<PlannerSubtask>::failure(advanced.error());
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "INSERT INTO "
        "planner_subtasks(id,workspace_id,task_id,source_subtask_id,title,is_completed,position,"
        "created_at_utc,updated_at_utc) SELECT "
        "?,?,?,NULL,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'),strftime('%Y-%m-%dT%H:%M:%fZ','now')"
        " WHERE EXISTS(SELECT 1 FROM planner_tasks t JOIN workspaces w ON w.id=t.workspace_id "
        "WHERE t.id=? AND t.workspace_id=? AND w.account_id=?)"));
    query.addBindValue(subtask.id);
    query.addBindValue(workspaceId);
    query.addBindValue(taskId);
    query.addBindValue(subtask.title);
    query.addBindValue(subtask.completed ? 1 : 0);
    query.addBindValue(subtask.position);
    query.addBindValue(taskId);
    query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<PlannerSubtask>(query, QStringLiteral("add Planner subtask"));
    if (query.numRowsAffected() != 1)
        return missing<PlannerSubtask>(QStringLiteral("add Planner subtask"));
    auto committed = transaction.commit();
    if (!committed)
        return Result<PlannerSubtask>::failure(committed.error());
    return Result<PlannerSubtask>::success(std::move(subtask));
}

Result<PlannerSubtask> SqlPlannerRepository::updateSubtask(const QString &workspaceId,
                                                           const QString &taskId,
                                                           PlannerSubtask subtask,
                                                           qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<PlannerSubtask>(QStringLiteral("update Planner subtask"));
    if (subtask.title.trimmed().isEmpty() || subtask.position < 0)
        return missing<PlannerSubtask>(QStringLiteral("update Planner subtask"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<PlannerSubtask>::failure(*transaction.error());
    auto advanced = advanceTaskRevision(m_database.database(), m_accountId, workspaceId, taskId,
                                        expectedRevision, QStringLiteral("update Planner subtask"));
    if (!advanced)
        return Result<PlannerSubtask>::failure(advanced.error());
    QSqlQuery query(m_database.database());
    query.prepare(
        QStringLiteral("UPDATE planner_subtasks SET "
                       "title=?,is_completed=?,position=?,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%"
                       "fZ','now') WHERE id=? AND task_id=? AND workspace_id=? AND workspace_id "
                       "IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(subtask.title);
    query.addBindValue(subtask.completed ? 1 : 0);
    query.addBindValue(subtask.position);
    query.addBindValue(subtask.id);
    query.addBindValue(taskId);
    query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<PlannerSubtask>(query, QStringLiteral("update Planner subtask"));
    if (query.numRowsAffected() != 1)
        return missing<PlannerSubtask>(QStringLiteral("update Planner subtask"));
    auto committed = transaction.commit();
    if (!committed)
        return Result<PlannerSubtask>::failure(committed.error());
    return Result<PlannerSubtask>::success(std::move(subtask));
}

Result<void> SqlPlannerRepository::removeSubtask(const QString &workspaceId, const QString &taskId,
                                                 const QString &subtaskId, qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<void>(QStringLiteral("delete Planner subtask"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<void>::failure(*transaction.error());
    auto advanced = advanceTaskRevision(m_database.database(), m_accountId, workspaceId, taskId,
                                        expectedRevision, QStringLiteral("delete Planner subtask"));
    if (!advanced)
        return advanced;
    QSqlQuery query(m_database.database());
    query.prepare(
        QStringLiteral("DELETE FROM planner_subtasks WHERE id=? AND task_id=? AND workspace_id=? "
                       "AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(subtaskId);
    query.addBindValue(taskId);
    query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<void>(query, QStringLiteral("delete Planner subtask"));
    if (query.numRowsAffected() != 1)
        return missing<void>(QStringLiteral("delete Planner subtask"));
    return transaction.commit();
}

Result<void> SqlPlannerRepository::reorderSubtasks(const QString &workspaceId,
                                                   const QString &taskId,
                                                   const QStringList &subtaskIds,
                                                   qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<void>(QStringLiteral("reorder Planner subtasks"));
    if (!uniqueIds(subtaskIds))
        return missing<void>(QStringLiteral("reorder Planner subtasks"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<void>::failure(*transaction.error());
    QSqlQuery count(m_database.database());
    count.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM planner_subtasks s JOIN workspaces w ON w.id=s.workspace_id WHERE "
        "s.workspace_id=? AND s.task_id=? AND w.account_id=?"));
    count.addBindValue(workspaceId);
    count.addBindValue(taskId);
    count.addBindValue(m_accountId.toString());
    if (!count.exec() || !count.next())
        return sqlFailure<void>(count, QStringLiteral("validate Planner subtask order"));
    if (count.value(0).toInt() != subtaskIds.size())
        return missing<void>(QStringLiteral("reorder Planner subtasks"));
    auto advanced =
        advanceTaskRevision(m_database.database(), m_accountId, workspaceId, taskId,
                            expectedRevision, QStringLiteral("reorder Planner subtasks"));
    if (!advanced)
        return advanced;
    QSqlQuery update(m_database.database());
    update.prepare(QStringLiteral(
        "UPDATE planner_subtasks SET "
        "position=?,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND task_id=? "
        "AND workspace_id=? AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    for (qsizetype i = 0; i < subtaskIds.size(); ++i)
    {
        update.bindValue(0, static_cast<int>(i));
        update.bindValue(1, subtaskIds.at(i));
        update.bindValue(2, taskId);
        update.bindValue(3, workspaceId);
        update.bindValue(4, m_accountId.toString());
        if (!update.exec() || update.numRowsAffected() != 1)
            return update.lastError().isValid()
                       ? sqlFailure<void>(update, QStringLiteral("reorder Planner subtasks"))
                       : missing<void>(QStringLiteral("reorder Planner subtasks"));
        update.finish();
    }
    return transaction.commit();
}

Result<domain::planner::PlannerTimeEntry> SqlPlannerRepository::addTimeEntry(
    domain::planner::PlannerTimeEntry entry, qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<domain::planner::PlannerTimeEntry>(QStringLiteral("record Planner time"));
    if (entry.durationMinutes <= 0 || expectedRevision < 1)
        return missing<domain::planner::PlannerTimeEntry>(QStringLiteral("record Planner time"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<domain::planner::PlannerTimeEntry>::failure(*transaction.error());
    QSqlQuery update(m_database.database());
    update.prepare(QStringLiteral(
        "UPDATE planner_tasks SET actual_tracked_minutes=actual_tracked_minutes+?,revision=revision+1,"
        "updated_at_utc=? WHERE id=? AND workspace_id=? AND revision=? AND actual_tracked_minutes<=? "
        "AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=? AND archived_at_utc IS NULL)"));
    update.addBindValue(entry.durationMinutes);
    update.addBindValue(timestamp(entry.recordedAtUtc));
    update.addBindValue(entry.taskId);
    update.addBindValue(entry.workspaceId);
    update.addBindValue(expectedRevision);
    update.addBindValue(std::numeric_limits<qint64>::max() - entry.durationMinutes);
    update.addBindValue(m_accountId.toString());
    if (!update.exec())
        return sqlFailure<domain::planner::PlannerTimeEntry>(update, QStringLiteral("update tracked Planner time"));
    if (update.numRowsAffected() != 1)
        return conflict<domain::planner::PlannerTimeEntry>(QStringLiteral("update tracked Planner time"));
    QSqlQuery insert(m_database.database());
    insert.prepare(QStringLiteral(
        "INSERT INTO planner_time_entries(id,workspace_id,task_id,duration_minutes,note,recorded_at_utc) "
        "VALUES(?,?,?,?,?,?)"));
    insert.addBindValue(entry.id);
    insert.addBindValue(entry.workspaceId);
    insert.addBindValue(entry.taskId);
    insert.addBindValue(entry.durationMinutes);
    insert.addBindValue(textValue(entry.note));
    insert.addBindValue(timestamp(entry.recordedAtUtc));
    if (!insert.exec())
        return sqlFailure<domain::planner::PlannerTimeEntry>(insert, QStringLiteral("save Planner time entry"));
    auto committed = transaction.commit();
    if (!committed) return Result<domain::planner::PlannerTimeEntry>::failure(committed.error());
    return Result<domain::planner::PlannerTimeEntry>::success(std::move(entry));
}

Result<QVector<domain::planner::PlannerTimeEntry>> SqlPlannerRepository::listTimeEntries(
    const QString &workspaceId, const QString &taskId)
{
    if (!onOwnerThread())
        return threadFailure<QVector<domain::planner::PlannerTimeEntry>>(QStringLiteral("list Planner time entries"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "SELECT e.id,e.workspace_id,e.task_id,e.duration_minutes,e.note,e.recorded_at_utc "
        "FROM planner_time_entries e JOIN planner_tasks t ON t.workspace_id=e.workspace_id AND t.id=e.task_id "
        "JOIN workspaces w ON w.id=t.workspace_id WHERE e.workspace_id=? AND e.task_id=? AND w.account_id=? "
        "ORDER BY e.recorded_at_utc DESC,e.id DESC"));
    query.addBindValue(workspaceId);
    query.addBindValue(taskId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<QVector<domain::planner::PlannerTimeEntry>>(query, QStringLiteral("list Planner time entries"));
    QVector<domain::planner::PlannerTimeEntry> entries;
    while (query.next())
        entries.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                        query.value(3).toLongLong(), query.value(4).toString(), readTimestamp(query.value(5))});
    return Result<QVector<domain::planner::PlannerTimeEntry>>::success(std::move(entries));
}

Result<void> SqlPlannerRepository::removeTimeEntry(const QString &workspaceId, const QString &taskId,
                                                   const QString &entryId, qint64 expectedRevision)
{
    if (!onOwnerThread()) return threadFailure<void>(QStringLiteral("remove Planner time entry"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return Result<void>::failure(*transaction.error());
    QSqlQuery read(m_database.database());
    read.prepare(QStringLiteral(
        "SELECT duration_minutes FROM planner_time_entries WHERE workspace_id=? AND task_id=? AND id=?"));
    read.addBindValue(workspaceId);
    read.addBindValue(taskId);
    read.addBindValue(entryId);
    if (!read.exec()) return sqlFailure<void>(read, QStringLiteral("read Planner time entry"));
    if (!read.next()) return missing<void>(QStringLiteral("read Planner time entry"));
    const qint64 duration = read.value(0).toLongLong();
    read.finish();
    QSqlQuery update(m_database.database());
    update.prepare(QStringLiteral(
        "UPDATE planner_tasks SET actual_tracked_minutes=actual_tracked_minutes-?,revision=revision+1,"
        "updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND workspace_id=? AND revision=? "
        "AND actual_tracked_minutes>=? AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=? "
        "AND archived_at_utc IS NULL)"));
    update.addBindValue(duration);
    update.addBindValue(taskId);
    update.addBindValue(workspaceId);
    update.addBindValue(expectedRevision);
    update.addBindValue(duration);
    update.addBindValue(m_accountId.toString());
    if (!update.exec()) return sqlFailure<void>(update, QStringLiteral("remove Planner tracked time"));
    if (update.numRowsAffected() != 1) return conflict<void>(QStringLiteral("remove Planner tracked time"));
    QSqlQuery remove(m_database.database());
    remove.prepare(QStringLiteral("DELETE FROM planner_time_entries WHERE workspace_id=? AND task_id=? AND id=?"));
    remove.addBindValue(workspaceId);
    remove.addBindValue(taskId);
    remove.addBindValue(entryId);
    if (!remove.exec()) return sqlFailure<void>(remove, QStringLiteral("delete Planner time entry"));
    if (remove.numRowsAffected() != 1) return missing<void>(QStringLiteral("delete Planner time entry"));
    return transaction.commit();
}

Result<PlannerActionRun> SqlPlannerRepository::beginActionRun(PlannerActionRun run)
{
    if (!onOwnerThread())
        return threadFailure<PlannerActionRun>(QStringLiteral("start Planner action"));
    const QString actionPrefix = QStringLiteral("service.start:");
    const QString serviceId = run.actionId.startsWith(actionPrefix)
        ? run.actionId.mid(actionPrefix.size()) : QString{};
    if (serviceId.isEmpty() || QUuid::fromString(serviceId).isNull() || run.id.isEmpty()
        || run.workspaceId.isEmpty() || run.taskId.isEmpty() || run.logReference.size() > 160
        || (!run.logReference.isEmpty()
            && run.logReference != QStringLiteral("service:%1:stdout").arg(serviceId)))
        return missing<PlannerActionRun>(QStringLiteral("start Planner action"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "INSERT INTO planner_action_runs(id,workspace_id,task_id,action_id,started_at_utc,log_reference) "
        "SELECT ?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM planner_tasks t JOIN workspaces w "
        "ON w.id=t.workspace_id WHERE t.workspace_id=? AND t.id=? AND t.linked_service_id<>'' "
        "AND t.linked_service_id=? AND w.account_id=? AND w.archived_at_utc IS NULL)"));
    query.addBindValue(run.id);
    query.addBindValue(run.workspaceId);
    query.addBindValue(run.taskId);
    query.addBindValue(run.actionId);
    query.addBindValue(timestamp(run.startedAtUtc));
    query.addBindValue(textValue(run.logReference));
    query.addBindValue(run.workspaceId);
    query.addBindValue(run.taskId);
    query.addBindValue(serviceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<PlannerActionRun>(query, QStringLiteral("start Planner action"));
    if (query.numRowsAffected() != 1)
        return missing<PlannerActionRun>(QStringLiteral("start Planner action"));
    QSqlQuery prune(m_database.database());
    prune.prepare(QStringLiteral(
        "DELETE FROM planner_action_runs WHERE workspace_id=? AND task_id=? AND finished_at_utc IS NOT NULL "
        "AND id NOT IN (SELECT id FROM planner_action_runs WHERE workspace_id=? AND task_id=? "
        "AND finished_at_utc IS NOT NULL ORDER BY started_at_utc DESC,id DESC LIMIT 20)"));
    prune.addBindValue(run.workspaceId);
    prune.addBindValue(run.taskId);
    prune.addBindValue(run.workspaceId);
    prune.addBindValue(run.taskId);
    if (!prune.exec())
        return sqlFailure<PlannerActionRun>(prune, QStringLiteral("prune Planner action history"));
    return Result<PlannerActionRun>::success(std::move(run));
}

Result<void> SqlPlannerRepository::finishActionRun(const QString& workspaceId, const QString& taskId,
                                                    const QString& runId, bool succeeded,
                                                    qint64 durationMilliseconds, QString errorSummary)
{
    if (!onOwnerThread()) return threadFailure<void>(QStringLiteral("finish Planner action"));
    if (durationMilliseconds < 0) return missing<void>(QStringLiteral("finish Planner action"));
    errorSummary = errorSummary.left(512);
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "UPDATE planner_action_runs SET finished_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now'),"
        "succeeded=?,duration_ms=?,error_summary=? WHERE workspace_id=? AND task_id=? AND id=? "
        "AND finished_at_utc IS NULL AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=? )"));
    query.addBindValue(succeeded ? 1 : 0);
    query.addBindValue(durationMilliseconds);
    query.addBindValue(textValue(errorSummary));
    query.addBindValue(workspaceId);
    query.addBindValue(taskId);
    query.addBindValue(runId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlFailure<void>(query, QStringLiteral("finish Planner action"));
    if (query.numRowsAffected() != 1) return conflict<void>(QStringLiteral("finish Planner action"));
    QSqlQuery prune(m_database.database());
    prune.prepare(QStringLiteral(
        "DELETE FROM planner_action_runs WHERE workspace_id=? AND task_id=? AND finished_at_utc IS NOT NULL "
        "AND id NOT IN (SELECT id FROM planner_action_runs WHERE workspace_id=? AND task_id=? "
        "AND finished_at_utc IS NOT NULL ORDER BY started_at_utc DESC,id DESC LIMIT 20)"));
    prune.addBindValue(workspaceId); prune.addBindValue(taskId);
    prune.addBindValue(workspaceId); prune.addBindValue(taskId);
    if (!prune.exec()) return sqlFailure<void>(prune, QStringLiteral("prune Planner action history"));
    return Result<void>::success();
}

Result<QVector<PlannerActionRun>> SqlPlannerRepository::listActionRuns(const QString& workspaceId,
                                                                        const QString& taskId,
                                                                        int limit)
{
    if (!onOwnerThread()) return threadFailure<QVector<PlannerActionRun>>(QStringLiteral("list Planner actions"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "SELECT r.id,r.workspace_id,r.task_id,r.action_id,r.started_at_utc,r.finished_at_utc,"
        "r.succeeded,r.duration_ms,r.error_summary,r.log_reference FROM planner_action_runs r "
        "JOIN planner_tasks t ON t.workspace_id=r.workspace_id AND t.id=r.task_id "
        "JOIN workspaces w ON w.id=t.workspace_id WHERE r.workspace_id=? AND r.task_id=? "
        "AND w.account_id=? ORDER BY CASE WHEN r.finished_at_utc IS NULL THEN 0 ELSE 1 END,"
        "r.started_at_utc DESC,r.id DESC LIMIT ?"));
    query.addBindValue(workspaceId);
    query.addBindValue(taskId);
    query.addBindValue(m_accountId.toString());
    query.addBindValue(qBound(1, limit, 20));
    if (!query.exec()) return sqlFailure<QVector<PlannerActionRun>>(query, QStringLiteral("list Planner actions"));
    QVector<PlannerActionRun> rows;
    while (query.next()) {
        PlannerActionRun row;
        row.id = query.value(0).toString(); row.workspaceId = query.value(1).toString();
        row.taskId = query.value(2).toString(); row.actionId = query.value(3).toString();
        row.startedAtUtc = readTimestamp(query.value(4));
        if (!query.value(5).isNull()) row.finishedAtUtc = readTimestamp(query.value(5));
        if (!query.value(6).isNull()) row.succeeded = query.value(6).toBool();
        row.durationMilliseconds = query.value(7).toLongLong();
        row.errorSummary = query.value(8).toString(); row.logReference = query.value(9).toString();
        rows.append(std::move(row));
    }
    return Result<QVector<PlannerActionRun>>::success(std::move(rows));
}

Result<PlannerMilestone> SqlPlannerRepository::createMilestone(PlannerMilestone milestone)
{
    if (!onOwnerThread())
        return threadFailure<PlannerMilestone>(QStringLiteral("create Planner milestone"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "INSERT INTO "
        "planner_milestones(id,workspace_id,title,description,start_date,due_date,is_completed,"
        "revision,created_at_utc,updated_at_utc) SELECT ?,?,?,?,?,?,?,?, ?,? WHERE EXISTS(SELECT 1 "
        "FROM workspaces WHERE id=? AND account_id=?)"));
    query.addBindValue(milestone.id);
    query.addBindValue(milestone.workspaceId);
    query.addBindValue(milestone.title);
    query.addBindValue(textValue(milestone.description));
    query.addBindValue(dateValue(milestone.startDate));
    query.addBindValue(dateValue(milestone.dueDate));
    query.addBindValue(milestone.completed ? 1 : 0);
    query.addBindValue(milestone.revision);
    query.addBindValue(timestamp(milestone.createdAtUtc));
    query.addBindValue(timestamp(milestone.updatedAtUtc));
    query.addBindValue(milestone.workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<PlannerMilestone>(query, QStringLiteral("create Planner milestone"));
    if (query.numRowsAffected() != 1)
        return missing<PlannerMilestone>(QStringLiteral("create Planner milestone"));
    return Result<PlannerMilestone>::success(std::move(milestone));
}

Result<QVector<PlannerMilestone>> SqlPlannerRepository::listMilestones(const QString &workspaceId)
{
    if (!onOwnerThread())
        return threadFailure<QVector<PlannerMilestone>>(QStringLiteral("list Planner milestones"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "SELECT "
        "m.id,m.workspace_id,m.title,m.description,m.start_date,m.due_date,m.is_completed,m."
        "revision,m.created_at_utc,m.updated_at_utc,"
        "(SELECT COUNT(*) FROM planner_tasks t WHERE t.workspace_id=m.workspace_id AND t.milestone_id=m.id),"
        "(SELECT COUNT(*) FROM planner_tasks t WHERE t.workspace_id=m.workspace_id AND t.milestone_id=m.id AND t.is_completed=1) "
        "FROM planner_milestones m JOIN workspaces w ON "
        "w.id=m.workspace_id WHERE m.workspace_id=? AND w.account_id=? ORDER BY m.due_date,m.title "
        "COLLATE NOCASE,m.id"));
    query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<QVector<PlannerMilestone>>(query,
                                                     QStringLiteral("list Planner milestones"));
    QVector<PlannerMilestone> milestones;
    while (query.next())
        milestones.append({query.value(0).toString(), query.value(1).toString(),
                           query.value(2).toString(), query.value(3).toString(),
                           readDate(query.value(4)), readDate(query.value(5)),
                           query.value(6).toBool(), query.value(7).toLongLong(),
                           readTimestamp(query.value(8)), readTimestamp(query.value(9)),
                           query.value(10).toInt(), query.value(11).toInt()});
    return Result<QVector<PlannerMilestone>>::success(std::move(milestones));
}

Result<PlannerMilestone> SqlPlannerRepository::updateMilestone(PlannerMilestone milestone,
                                                               qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<PlannerMilestone>(QStringLiteral("update Planner milestone"));
    QSqlQuery query(m_database.database());
    query.prepare(
        QStringLiteral("UPDATE planner_milestones SET "
                       "title=?,description=?,start_date=?,due_date=?,is_completed=?,revision="
                       "revision+1,updated_at_utc=? WHERE id=? AND workspace_id=? AND revision=? "
                       "AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(milestone.title);
    query.addBindValue(textValue(milestone.description));
    query.addBindValue(dateValue(milestone.startDate));
    query.addBindValue(dateValue(milestone.dueDate));
    query.addBindValue(milestone.completed ? 1 : 0);
    query.addBindValue(timestamp(milestone.updatedAtUtc));
    query.addBindValue(milestone.id);
    query.addBindValue(milestone.workspaceId);
    query.addBindValue(expectedRevision);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<PlannerMilestone>(query, QStringLiteral("update Planner milestone"));
    if (query.numRowsAffected() != 1)
        return conflict<PlannerMilestone>(QStringLiteral("update Planner milestone"));
    milestone.revision = expectedRevision + 1;
    return Result<PlannerMilestone>::success(std::move(milestone));
}

Result<void> SqlPlannerRepository::removeMilestone(const QString &workspaceId,
                                                   const QString &milestoneId,
                                                   qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<void>(QStringLiteral("delete Planner milestone"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<void>::failure(*transaction.error());
    QSqlQuery clear(m_database.database());
    clear.prepare(
        QStringLiteral("UPDATE planner_tasks SET "
                       "milestone_id=NULL,revision=revision+1,updated_at_utc=strftime('%Y-%m-%dT%H:"
                       "%M:%fZ','now') WHERE workspace_id=? AND milestone_id=? AND workspace_id "
                       "IN(SELECT id FROM workspaces WHERE account_id=?)"));
    clear.addBindValue(workspaceId);
    clear.addBindValue(milestoneId);
    clear.addBindValue(m_accountId.toString());
    if (!clear.exec())
        return sqlFailure<void>(clear, QStringLiteral("unlink Planner milestone"));
    QSqlQuery remove(m_database.database());
    remove.prepare(QStringLiteral(
        "DELETE FROM planner_milestones WHERE workspace_id=? AND id=? AND revision=? AND "
        "workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    remove.addBindValue(workspaceId);
    remove.addBindValue(milestoneId);
    remove.addBindValue(expectedRevision);
    remove.addBindValue(m_accountId.toString());
    if (!remove.exec())
        return sqlFailure<void>(remove, QStringLiteral("delete Planner milestone"));
    if (remove.numRowsAffected() != 1)
        return conflict<void>(QStringLiteral("delete Planner milestone"));
    return transaction.commit();
}

Result<void> SqlPlannerRepository::addDependency(const QString &workspaceId, const QString &taskId,
                                                 const QString &dependsOnTaskId,
                                                 qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<void>(QStringLiteral("add Planner dependency"));
    if (taskId == dependsOnTaskId)
        return missing<void>(QStringLiteral("add Planner dependency"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<void>::failure(*transaction.error());
    QSqlQuery endpoints(m_database.database());
    endpoints.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM planner_tasks t JOIN workspaces w ON w.id=t.workspace_id WHERE "
        "t.workspace_id=? AND t.id IN(?,?) AND w.account_id=?"));
    endpoints.addBindValue(workspaceId);
    endpoints.addBindValue(taskId);
    endpoints.addBindValue(dependsOnTaskId);
    endpoints.addBindValue(m_accountId.toString());
    if (!endpoints.exec() || !endpoints.next())
        return sqlFailure<void>(endpoints, QStringLiteral("validate Planner dependency"));
    if (endpoints.value(0).toInt() != 2)
        return missing<void>(QStringLiteral("add Planner dependency"));
    endpoints.finish();
    QSqlQuery graphQuery(m_database.database());
    graphQuery.prepare(QStringLiteral(
        "SELECT task_id,depends_on_task_id FROM planner_task_dependencies WHERE workspace_id=?"));
    graphQuery.addBindValue(workspaceId);
    if (!graphQuery.exec())
        return sqlFailure<void>(graphQuery, QStringLiteral("read Planner dependency graph"));
    QVector<QPair<QString, QString>> edges;
    while (graphQuery.next())
        edges.append({graphQuery.value(0).toString(), graphQuery.value(1).toString()});
    graphQuery.finish();
    if (domain::planner::PlannerRules::wouldCreateDependencyCycle(edges, taskId, dependsOnTaskId))
        return Result<void>::failure(
            AppError::failure(ErrorCode::InvalidArgument, ErrorCategory::Workspace,
                              QStringLiteral("That dependency would create a cycle."),
                              QStringLiteral("Dependency graph contains a path back to the task."),
                              QStringLiteral("add Planner dependency")));
    auto advanced = advanceTaskRevision(m_database.database(), m_accountId, workspaceId, taskId,
                                        expectedRevision, QStringLiteral("add Planner dependency"));
    if (!advanced)
        return advanced;
    QSqlQuery insert(m_database.database());
    insert.prepare(
        QStringLiteral("INSERT INTO "
                       "planner_task_dependencies(workspace_id,task_id,depends_on_task_id,created_"
                       "at_utc) VALUES(?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'))"));
    insert.addBindValue(workspaceId);
    insert.addBindValue(taskId);
    insert.addBindValue(dependsOnTaskId);
    if (!insert.exec())
        return sqlFailure<void>(insert, QStringLiteral("add Planner dependency"));
    return transaction.commit();
}

Result<void> SqlPlannerRepository::removeDependency(const QString &workspaceId,
                                                    const QString &taskId,
                                                    const QString &dependsOnTaskId,
                                                    qint64 expectedRevision)
{
    if (!onOwnerThread())
        return threadFailure<void>(QStringLiteral("remove Planner dependency"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active())
        return Result<void>::failure(*transaction.error());
    auto advanced =
        advanceTaskRevision(m_database.database(), m_accountId, workspaceId, taskId,
                            expectedRevision, QStringLiteral("remove Planner dependency"));
    if (!advanced)
        return advanced;
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "DELETE FROM planner_task_dependencies WHERE workspace_id=? AND task_id=? AND "
        "depends_on_task_id=? AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(workspaceId);
    query.addBindValue(taskId);
    query.addBindValue(dependsOnTaskId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return sqlFailure<void>(query, QStringLiteral("remove Planner dependency"));
    return transaction.commit();
}

} // namespace devhub::infrastructure::planner
