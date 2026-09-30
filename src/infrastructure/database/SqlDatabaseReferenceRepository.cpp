#include "SqlDatabaseReferenceRepository.h"

#include "SqlErrorMapper.h"

#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QUuid>

namespace devhub::infrastructure::database {
using namespace application::databases;

shared::common::Result<QVector<DatabaseReference>> SqlDatabaseReferenceRepository::list(const QString& workspaceId)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT d.id,d.workspace_id,d.display_name,d.database_path,d.engine_key "
        "FROM database_references d JOIN workspaces w ON w.id=d.workspace_id "
        "WHERE d.workspace_id=? AND d.engine_key='sqlite' AND w.account_id=? ORDER BY d.display_name COLLATE NOCASE"));
    query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return shared::common::Result<QVector<DatabaseReference>>::failure(
        SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("list database references")));
    QVector<DatabaseReference> values;
    while (query.next()) {
        DatabaseReference value{query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                                query.value(3).toString(), query.value(4).toString(), false};
        QFile file(value.filePath);
        if (file.open(QIODevice::ReadOnly)) value.headerVerified = file.read(16) == QByteArrayLiteral("SQLite format 3\0");
        values.append(std::move(value));
    }
    return shared::common::Result<QVector<DatabaseReference>>::success(std::move(values));
}

shared::common::Result<void> SqlDatabaseReferenceRepository::add(const DatabaseReference& reference)
{
    if (reference.workspaceId.trimmed().isEmpty() || reference.displayName.trimmed().isEmpty()
        || reference.filePath.trimmed().isEmpty()) {
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Database,
            QStringLiteral("Enter a database name and choose a file."), QStringLiteral("Database reference fields were incomplete."),
            QStringLiteral("add database reference")));
    }
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO database_references(id,workspace_id,display_name,engine_key,database_path) "
        "SELECT ?,?,?,'sqlite',? WHERE EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=?)"));
    query.addBindValue(reference.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : reference.id);
    query.addBindValue(reference.workspaceId); query.addBindValue(reference.displayName.trimmed());
    query.addBindValue(QFileInfo(reference.filePath).absoluteFilePath());
    query.addBindValue(reference.workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(
        SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("add database reference")));
    if (query.numRowsAffected() == 0) return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Permission,
        QStringLiteral("The database reference does not belong to an active workspace."), reference.workspaceId,
        QStringLiteral("add database reference")));
    return shared::common::Result<void>::success();
}

shared::common::Result<void> SqlDatabaseReferenceRepository::remove(const QString& id, const QString& workspaceId)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("DELETE FROM database_references WHERE id=? AND workspace_id=? "
        "AND workspace_id IN (SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(id); query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(
        SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("remove database reference")));
    return shared::common::Result<void>::success();
}

} // namespace devhub::infrastructure::database
