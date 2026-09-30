#include "SqlLogSourceRepository.h"

#include "../database/SqlErrorMapper.h"

#include <QFileInfo>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

namespace devhub::infrastructure::logs {
namespace {
using Source = application::logs::LogSourceConfig;

shared::common::Result<void> invalid(const QString& message, const QString& details)
{
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Log,
        message, details, QStringLiteral("validate log source")));
}
}

shared::common::Result<QVector<Source>> SqlLogSourceRepository::list(const QString& workspaceId)
{
    if (QThread::currentThread() != m_database.thread() || !m_database.database().isOpen()) {
        return shared::common::Result<QVector<Source>>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::DatabaseUnavailable, domain::errors::ErrorCategory::Database,
            QStringLiteral("Log source storage is unavailable."), QStringLiteral("Database is closed or accessed from the wrong thread."),
            QStringLiteral("list log sources")));
    }
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT l.id,l.workspace_id,l.display_name,l.file_path,l.format_key "
        "FROM log_sources l JOIN workspaces w ON w.id=l.workspace_id "
        "WHERE l.workspace_id=? AND w.account_id=? ORDER BY l.display_name COLLATE NOCASE"));
    query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec()) return shared::common::Result<QVector<Source>>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("list log sources")));
    QVector<Source> sources;
    while (query.next()) sources.append({query.value(0).toString(), query.value(1).toString(),
        query.value(2).toString(), query.value(3).toString(), query.value(4).toString(), application::logs::LogSourceKind::File});
    return shared::common::Result<QVector<Source>>::success(std::move(sources));
}

shared::common::Result<void> SqlLogSourceRepository::add(const Source& source)
{
    if (source.workspaceId.trimmed().isEmpty() || source.displayName.trimmed().isEmpty() || source.filePath.trimmed().isEmpty())
        return invalid(QStringLiteral("Enter a name and choose a log file."), QStringLiteral("Required log source field was blank."));
    const QFileInfo file(source.filePath);
    if (!file.exists() || !file.isFile() || !file.isReadable())
        return invalid(QStringLiteral("The selected log file is unavailable or unreadable."), source.filePath);
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO log_sources(id,workspace_id,display_name,file_path,format_key) "
        "SELECT ?,?,?,?,'text' WHERE EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=?)"));
    const QString id = source.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : source.id;
    query.addBindValue(id);
    query.addBindValue(source.workspaceId);
    query.addBindValue(source.displayName.trimmed());
    query.addBindValue(QFileInfo(source.filePath).absoluteFilePath());
    query.addBindValue(source.workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("add log source")));
    if (query.numRowsAffected() == 0) return invalid(QStringLiteral("The log source does not belong to an active workspace."), source.workspaceId);
    return shared::common::Result<void>::success();
}

shared::common::Result<void> SqlLogSourceRepository::remove(const QString& sourceId, const QString& workspaceId)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("DELETE FROM log_sources WHERE id=? AND workspace_id=? "
        "AND workspace_id IN (SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(sourceId);
    query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("remove log source")));
    return shared::common::Result<void>::success();
}

} // namespace devhub::infrastructure::logs
