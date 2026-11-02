#include "SqlTaskNoteLinkRepository.h"

#include "../database/SqlErrorMapper.h"
#include "../../domain/errors/AppError.h"

#include <QSqlQuery>

namespace devhub::infrastructure::planner {
namespace {
using application::planner::TaskNoteLink;
using shared::common::Result;

template<class T>
Result<T> unavailable(const QString& message, const QString& operation)
{
    return Result<T>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
        message, {}, operation));
}

Result<QVector<TaskNoteLink>> listLinks(database::InternalDatabaseManager& database,
    const QString& accountId, const QString& workspaceId, const QString& ownerColumn,
    const QString& ownerId, const QString& orderColumn)
{
    QSqlQuery query(database.database());
    QString sql = QStringLiteral("SELECT l.workspace_id,l.task_id,l.note_id,l.created_at_utc "
        "FROM planner_task_note_links l JOIN workspaces w ON w.id=l.workspace_id "
        "WHERE l.workspace_id=? AND l.%1=? AND w.account_id=? AND w.archived_at_utc IS NULL "
        "ORDER BY l.created_at_utc,%2");
    sql = sql.arg(ownerColumn).arg(orderColumn);
    query.prepare(sql);
    query.addBindValue(workspaceId);
    query.addBindValue(ownerId);
    query.addBindValue(accountId);
    if (!query.exec())
        return Result<QVector<TaskNoteLink>>::failure(database::SqlErrorMapper::fromSqlError(
            query.lastError(), QStringLiteral("list Planner note links")));
    QVector<TaskNoteLink> result;
    while (query.next())
        result.append({query.value(0).toString(), query.value(1).toString(),
            query.value(2).toString(), query.value(3).toString()});
    return Result<QVector<TaskNoteLink>>::success(std::move(result));
}
}

Result<void> SqlTaskNoteLinkRepository::requireActiveWorkspace(const QString& workspaceId)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT 1 FROM workspaces WHERE id=? AND account_id=? AND archived_at_utc IS NULL"));
    query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return Result<void>::failure(database::SqlErrorMapper::fromSqlError(
            query.lastError(), QStringLiteral("validate Planner note workspace")));
    if (query.next()) return Result<void>::success();
    return unavailable<void>(QStringLiteral("Select an active workspace owned by this account."),
                             QStringLiteral("validate Planner note workspace"));
}

Result<void> SqlTaskNoteLinkRepository::add(const QString& workspaceId,
    const QString& taskId, const QString& noteId)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO planner_task_note_links(workspace_id,task_id,note_id) "
        "SELECT ?,?,? WHERE EXISTS(SELECT 1 FROM workspaces w WHERE w.id=? AND w.account_id=? "
        "AND w.archived_at_utc IS NULL) "
        "AND EXISTS(SELECT 1 FROM planner_tasks t WHERE t.workspace_id=? AND t.id=?) "
        "AND EXISTS(SELECT 1 FROM note_nodes n WHERE n.workspace_id=? AND n.id=? "
        "AND n.node_type='note' AND n.is_trashed=0)"));
    query.addBindValue(workspaceId); query.addBindValue(taskId); query.addBindValue(noteId);
    query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    query.addBindValue(workspaceId); query.addBindValue(taskId);
    query.addBindValue(workspaceId); query.addBindValue(noteId);
    if (!query.exec())
        return Result<void>::failure(database::SqlErrorMapper::fromSqlError(
            query.lastError(), QStringLiteral("link note to Planner task")));
    if (query.numRowsAffected() == 1) return Result<void>::success();

    QSqlQuery duplicate(m_database.database());
    duplicate.prepare(QStringLiteral("SELECT 1 FROM planner_task_note_links l JOIN workspaces w ON w.id=l.workspace_id "
        "WHERE l.workspace_id=? AND l.task_id=? AND l.note_id=? AND w.account_id=? AND w.archived_at_utc IS NULL"));
    duplicate.addBindValue(workspaceId); duplicate.addBindValue(taskId); duplicate.addBindValue(noteId);
    duplicate.addBindValue(m_accountId.toString());
    if (duplicate.exec() && duplicate.next())
        return unavailable<void>(QStringLiteral("This note is already linked to the task."),
                                 QStringLiteral("link note to Planner task"));
    return unavailable<void>(QStringLiteral("The task or note is no longer available in this workspace."),
                             QStringLiteral("link note to Planner task"));
}

Result<void> SqlTaskNoteLinkRepository::remove(const QString& workspaceId,
    const QString& taskId, const QString& noteId)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("DELETE FROM planner_task_note_links WHERE workspace_id=? AND task_id=? "
        "AND note_id=? AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=? AND archived_at_utc IS NULL)"));
    query.addBindValue(workspaceId); query.addBindValue(taskId); query.addBindValue(noteId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec())
        return Result<void>::failure(database::SqlErrorMapper::fromSqlError(
            query.lastError(), QStringLiteral("unlink note from Planner task")));
    return query.numRowsAffected() == 1 ? Result<void>::success()
        : unavailable<void>(QStringLiteral("This task-note link is no longer available."),
                            QStringLiteral("unlink note from Planner task"));
}

Result<QVector<TaskNoteLink>> SqlTaskNoteLinkRepository::listForTask(
    const QString& workspaceId, const QString& taskId)
{
    return listLinks(m_database, m_accountId.toString(), workspaceId, QStringLiteral("task_id"),
                     taskId, QStringLiteral("l.note_id"));
}

Result<QVector<TaskNoteLink>> SqlTaskNoteLinkRepository::listForNote(
    const QString& workspaceId, const QString& noteId)
{
    return listLinks(m_database, m_accountId.toString(), workspaceId, QStringLiteral("note_id"),
                     noteId, QStringLiteral("l.task_id"));
}

} // namespace devhub::infrastructure::planner
