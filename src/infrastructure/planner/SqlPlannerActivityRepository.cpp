#include "SqlPlannerActivityRepository.h"

#include "../database/SqlErrorMapper.h"
#include "../database/SqlTransactionGuard.h"
#include "../../domain/errors/AppError.h"
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>

namespace devhub::infrastructure::planner
{
using namespace domain::planner;
using shared::common::Result;

namespace
{
template <typename T> Result<T> sqlError(const QSqlQuery& query, const QString& operation)
{ return Result<T>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), operation)); }

template <typename T> Result<T> unavailable(const QString& operation)
{
    return Result<T>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
        QStringLiteral("The requested Planner activity is unavailable in this workspace."), {}, operation));
}

QDateTime dateTime(const QVariant& value)
{
    auto parsed = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!parsed.isValid()) parsed = QDateTime::fromString(value.toString(), Qt::ISODate);
    return parsed.toUTC();
}
}

bool SqlPlannerActivityRepository::onOwnerThread() const
{ return QThread::currentThread() == m_database.thread(); }

Result<QVector<PlannerActivityEvent>> SqlPlannerActivityRepository::listEvents(
    const QString& workspaceId, const QString& taskId, int limit, int offset)
{
    if (!onOwnerThread()) return unavailable<QVector<PlannerActivityEvent>>(QStringLiteral("read Planner activity"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "SELECT e.id,e.workspace_id,e.task_id,e.event_type,e.occurred_at_utc,e.observed_at_utc,"
        "e.origin,e.description,e.metadata_json,COALESCE(e.external_ref_type,''),COALESCE(e.external_ref_id,'') "
        "FROM planner_activity_events e JOIN workspaces w ON w.id=e.workspace_id "
        "JOIN planner_tasks t ON t.id=e.task_id AND t.workspace_id=e.workspace_id "
        "WHERE e.workspace_id=? AND e.task_id=? AND w.account_id=? AND w.archived_at_utc IS NULL "
        "ORDER BY e.occurred_at_utc DESC,e.id DESC LIMIT ? OFFSET ?"));
    query.addBindValue(workspaceId); query.addBindValue(taskId); query.addBindValue(m_accountId.toString());
    query.addBindValue(qBound(1, limit, 100)); query.addBindValue(qMax(0, offset));
    if (!query.exec()) return sqlError<QVector<PlannerActivityEvent>>(query, QStringLiteral("read Planner activity"));
    QVector<PlannerActivityEvent> rows;
    while (query.next()) {
        PlannerActivityEvent event;
        event.id = query.value(0).toString(); event.workspaceId = query.value(1).toString();
        event.taskId = query.value(2).toString(); event.eventType = query.value(3).toString();
        event.occurredAtUtc = dateTime(query.value(4)); event.observedAtUtc = dateTime(query.value(5));
        event.origin = query.value(6).toString(); event.description = query.value(7).toString();
        event.metadataJson = query.value(8).toString(); event.externalRefType = query.value(9).toString();
        event.externalRefId = query.value(10).toString(); rows.append(std::move(event));
    }
    return Result<QVector<PlannerActivityEvent>>::success(std::move(rows));
}

Result<QVector<PlannerGitCommitAssociation>> SqlPlannerActivityRepository::listGitCommits(
    const QString& workspaceId, const QString& taskId)
{
    if (!onOwnerThread()) return unavailable<QVector<PlannerGitCommitAssociation>>(QStringLiteral("read Git task links"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "SELECT c.workspace_id,c.task_id,c.commit_sha,c.association_source,c.branch_name,c.subject,"
        "c.author,c.authored_at_utc,c.committed_at_utc,c.observed_at_utc,c.availability FROM planner_task_git_commits c "
        "JOIN workspaces w ON w.id=c.workspace_id JOIN planner_tasks t ON t.id=c.task_id AND t.workspace_id=c.workspace_id "
        "WHERE c.workspace_id=? AND c.task_id=? AND w.account_id=? AND w.archived_at_utc IS NULL "
        "ORDER BY COALESCE(c.authored_at_utc,c.observed_at_utc) DESC,c.commit_sha"));
    query.addBindValue(workspaceId); query.addBindValue(taskId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlError<QVector<PlannerGitCommitAssociation>>(query, QStringLiteral("read Git task links"));
    QVector<PlannerGitCommitAssociation> rows;
    while (query.next()) {
        PlannerGitCommitAssociation row;
        row.workspaceId=query.value(0).toString(); row.taskId=query.value(1).toString();
        row.commitSha=query.value(2).toString(); row.associationSource=query.value(3).toString();
        row.branchName=query.value(4).toString(); row.subject=query.value(5).toString(); row.author=query.value(6).toString();
        row.authoredAtUtc=dateTime(query.value(7)); row.committedAtUtc=dateTime(query.value(8));
        row.observedAtUtc=dateTime(query.value(9)); row.availability=query.value(10).toString(); rows.append(std::move(row));
    }
    return Result<QVector<PlannerGitCommitAssociation>>::success(std::move(rows));
}

Result<void> SqlPlannerActivityRepository::linkGitCommit(PlannerGitCommitAssociation association)
{
    if (!onOwnerThread()) return unavailable<void>(QStringLiteral("link Git commit to Planner task"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return Result<void>::failure(*transaction.error());
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral(
        "INSERT INTO planner_task_git_commits(workspace_id,task_id,commit_sha,association_source,branch_name,"
        "subject,author,authored_at_utc,committed_at_utc,observed_at_utc,availability) SELECT ?,?,?,?,?,?,?,?,?,?, 'reachable' "
        "WHERE EXISTS(SELECT 1 FROM planner_tasks t JOIN workspaces w ON w.id=t.workspace_id "
        "WHERE t.id=? AND t.workspace_id=? AND w.account_id=? AND w.archived_at_utc IS NULL) "
        "ON CONFLICT(workspace_id,task_id,commit_sha) DO UPDATE SET availability='reachable',"
        "branch_name=CASE WHEN excluded.branch_name<>'' THEN excluded.branch_name ELSE planner_task_git_commits.branch_name END"));
    query.addBindValue(association.workspaceId); query.addBindValue(association.taskId);
    query.addBindValue(association.commitSha); query.addBindValue(association.associationSource);
    query.addBindValue(association.branchName.isNull() ? QStringLiteral("") : association.branchName);
    query.addBindValue(association.subject.isNull() ? QStringLiteral("") : association.subject);
    query.addBindValue(association.author.isNull() ? QStringLiteral("") : association.author);
    query.addBindValue(association.authoredAtUtc.isValid() ? association.authoredAtUtc.toUTC().toString(Qt::ISODateWithMs) : QVariant());
    query.addBindValue(association.committedAtUtc.isValid() ? association.committedAtUtc.toUTC().toString(Qt::ISODateWithMs) : QVariant());
    query.addBindValue(association.observedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(association.taskId); query.addBindValue(association.workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlError<void>(query, QStringLiteral("link Git commit to Planner task"));
    if (query.numRowsAffected() < 1) return unavailable<void>(QStringLiteral("link Git commit to Planner task"));
    query.finish();
    QSqlQuery event(m_database.database());
    event.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,"
        "observed_at_utc,origin,description,metadata_json,external_ref_type,external_ref_id,dedupe_key) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
    const QString kind = association.associationSource == QStringLiteral("manual") ? QStringLiteral("manual")
        : association.associationSource == QStringLiteral("message") ? QStringLiteral("message-reference") : QStringLiteral("branch-review");
    event.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces)); event.addBindValue(association.workspaceId);
    event.addBindValue(association.taskId); event.addBindValue(QStringLiteral("git.commit"));
    event.addBindValue(association.authoredAtUtc.isValid() ? association.authoredAtUtc.toUTC().toString(Qt::ISODateWithMs)
                                                           : association.observedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    event.addBindValue(association.observedAtUtc.toUTC().toString(Qt::ISODateWithMs)); event.addBindValue(kind);
    event.addBindValue(association.subject.isEmpty() ? QStringLiteral("Commit associated") : association.subject);
    QJsonObject metadata{{QStringLiteral("sha"), association.commitSha}, {QStringLiteral("author"), association.author},
                         {QStringLiteral("branch"), association.branchName}, {QStringLiteral("associationSource"), association.associationSource},
                         {QStringLiteral("committerAt"), association.committedAtUtc.isValid()
                             ? association.committedAtUtc.toUTC().toString(Qt::ISODateWithMs) : QString{}}};
    event.addBindValue(QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact)));
    event.addBindValue(QStringLiteral("git-commit")); event.addBindValue(association.commitSha);
    event.addBindValue(QStringLiteral("git-commit:%1").arg(association.commitSha));
    if (!event.exec()) return sqlError<void>(event, QStringLiteral("record Git task activity"));
    return transaction.commit();
}

Result<void> SqlPlannerActivityRepository::unlinkGitCommit(
    const QString& workspaceId, const QString& taskId, const QString& sha)
{
    if (!onOwnerThread()) return unavailable<void>(QStringLiteral("unlink Git commit from Planner task"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return Result<void>::failure(*transaction.error());
    QSqlQuery select(m_database.database());
    select.prepare(QStringLiteral("SELECT commit_sha,subject FROM planner_task_git_commits c JOIN workspaces w ON w.id=c.workspace_id "
                                  "WHERE c.workspace_id=? AND c.task_id=? AND c.commit_sha=? AND w.account_id=? AND w.archived_at_utc IS NULL"));
    select.addBindValue(workspaceId); select.addBindValue(taskId); select.addBindValue(sha); select.addBindValue(m_accountId.toString());
    if (!select.exec()) return sqlError<void>(select, QStringLiteral("unlink Git commit from Planner task"));
    if (!select.next()) return unavailable<void>(QStringLiteral("unlink Git commit from Planner task"));
    const QString commitSha = select.value(0).toString(); const QString subject = select.value(1).toString(); select.finish();
    QSqlQuery remove(m_database.database());
    remove.prepare(QStringLiteral("DELETE FROM planner_task_git_commits WHERE workspace_id=? AND task_id=? AND commit_sha=?"));
    remove.addBindValue(workspaceId); remove.addBindValue(taskId); remove.addBindValue(commitSha);
    if (!remove.exec()) return sqlError<void>(remove, QStringLiteral("unlink Git commit from Planner task"));
    QSqlQuery event(m_database.database());
    event.prepare(QStringLiteral("INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,"
        "observed_at_utc,origin,description,metadata_json,external_ref_type,external_ref_id) VALUES(?,?,?,'git.commit.unlinked',"
        "strftime('%Y-%m-%dT%H:%M:%fZ','now'),strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Git commit unlinked',?,'git-commit',?)"));
    event.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces)); event.addBindValue(workspaceId); event.addBindValue(taskId);
    QJsonObject metadata{{QStringLiteral("sha"),commitSha},{QStringLiteral("subject"),subject}};
    event.addBindValue(QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact))); event.addBindValue(commitSha);
    if (!event.exec()) return sqlError<void>(event, QStringLiteral("record Git commit unlink"));
    return transaction.commit();
}

Result<void> SqlPlannerActivityRepository::setGitCommitAvailability(
    const QString& workspaceId, const QString& taskId, const QString& sha,
    const QString& availability, const QDateTime& observedAtUtc)
{
    if (!onOwnerThread()) return unavailable<void>(QStringLiteral("update Git commit availability"));
    if (availability != QStringLiteral("reachable") && availability != QStringLiteral("unavailable"))
        return unavailable<void>(QStringLiteral("update Git commit availability"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return Result<void>::failure(*transaction.error());
    QSqlQuery current(m_database.database());
    current.prepare(QStringLiteral(
        "SELECT c.availability FROM planner_task_git_commits c JOIN workspaces w ON w.id=c.workspace_id "
        "WHERE c.workspace_id=? AND c.task_id=? AND c.commit_sha=? AND w.account_id=? AND w.archived_at_utc IS NULL"));
    current.addBindValue(workspaceId); current.addBindValue(taskId); current.addBindValue(sha);
    current.addBindValue(m_accountId.toString());
    if (!current.exec()) return sqlError<void>(current, QStringLiteral("read Git commit availability"));
    if (!current.next()) return unavailable<void>(QStringLiteral("update Git commit availability"));
    const QString previous = current.value(0).toString(); current.finish();
    if (previous == availability) return transaction.commit();
    const QString timestamp = observedAtUtc.toUTC().toString(Qt::ISODateWithMs);
    QSqlQuery update(m_database.database());
    update.prepare(QStringLiteral(
        "UPDATE planner_task_git_commits SET availability=?,observed_at_utc=? WHERE workspace_id=? AND task_id=? "
        "AND commit_sha=? AND availability=? AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=? AND archived_at_utc IS NULL)"));
    update.addBindValue(availability); update.addBindValue(timestamp); update.addBindValue(workspaceId);
    update.addBindValue(taskId); update.addBindValue(sha); update.addBindValue(previous); update.addBindValue(m_accountId.toString());
    if (!update.exec()) return sqlError<void>(update, QStringLiteral("update Git commit availability"));
    if (update.numRowsAffected() != 1) return unavailable<void>(QStringLiteral("update Git commit availability"));
    QSqlQuery event(m_database.database());
    event.prepare(QStringLiteral(
        "INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,observed_at_utc,origin,description,"
        "metadata_json,external_ref_type,external_ref_id) VALUES(?,?,?,'git.commit.availability',?,?,'git',?,?, 'git-commit',?)"));
    event.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces)); event.addBindValue(workspaceId);
    event.addBindValue(taskId); event.addBindValue(timestamp); event.addBindValue(timestamp);
    event.addBindValue(QStringLiteral("Commit availability changed"));
    event.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("sha"),sha},
        {QStringLiteral("from"),previous},{QStringLiteral("to"),availability}}).toJson(QJsonDocument::Compact)));
    event.addBindValue(sha);
    if (!event.exec()) return sqlError<void>(event, QStringLiteral("record Git commit availability"));
    return transaction.commit();
}

} // namespace devhub::infrastructure::planner
