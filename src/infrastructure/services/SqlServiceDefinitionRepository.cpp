#include "SqlServiceDefinitionRepository.h"
#include "../database/SqlErrorMapper.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSqlQuery>
#include <QThread>

namespace devhub::infrastructure::services {
namespace {
using Definition = domain::services::ServiceDefinition;
QString jsonStrings(const QStringList& values) {
    QJsonArray array; for (const auto& value : values) array.append(value);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
QString jsonPorts(const QVector<quint16>& values) {
    QJsonArray array; for (auto value : values) array.append(static_cast<int>(value));
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
QString restartName(domain::services::RestartPolicy policy) {
    switch (policy) { case domain::services::RestartPolicy::OnFailure: return QStringLiteral("on-failure");
    case domain::services::RestartPolicy::Always: return QStringLiteral("always"); default: return QStringLiteral("never"); }
}
shared::common::Result<void> dbFailure(const QSqlError& error, const QString& operation) {
    return shared::common::Result<void>::failure(database::SqlErrorMapper::fromSqlError(error, operation));
}
}
shared::common::Result<QVector<Definition>> SqlServiceDefinitionRepository::list(const QString& workspaceId) {
    if (QThread::currentThread() != m_database.thread() || !m_database.database().isOpen())
        return shared::common::Result<QVector<Definition>>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::DatabaseUnavailable, domain::errors::ErrorCategory::Database,
            QStringLiteral("Service storage is unavailable."), QStringLiteral("Database accessed from the wrong thread or closed."), QStringLiteral("list services")));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT s.id,s.workspace_id,s.display_name,s.executable_path,s.arguments_json,COALESCE(s.working_directory,''),"
        "s.shell_mode,s.environment_names_json,s.expected_ports_json,s.restart_policy,s.stop_timeout_ms FROM service_definitions s "
        "JOIN workspaces w ON w.id=s.workspace_id WHERE s.workspace_id=? AND w.account_id=? ORDER BY s.display_name COLLATE NOCASE"));
    query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return shared::common::Result<QVector<Definition>>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("list services")));
    QVector<Definition> definitions;
    while (query.next()) {
        Definition item; item.id=query.value(0).toString(); item.workspaceId=query.value(1).toString(); item.name=query.value(2).toString();
        item.program=query.value(3).toString(); item.workingDirectory=query.value(5).toString(); item.shellMode=query.value(6).toBool();
        const auto arguments=QJsonDocument::fromJson(query.value(4).toByteArray());
        const auto environment=QJsonDocument::fromJson(query.value(7).toByteArray());
        const auto ports=QJsonDocument::fromJson(query.value(8).toByteArray());
        for (const auto& value : arguments.array()) item.arguments.append(value.toString());
        for (const auto& value : environment.array()) item.environmentNames.append(value.toString());
        for (const auto& value : ports.array()) if (value.toInt() > 0 && value.toInt() <= 65535) item.expectedPorts.append(static_cast<quint16>(value.toInt()));
        const auto restart=query.value(9).toString();
        item.restartPolicy=restart==QStringLiteral("always") ? domain::services::RestartPolicy::Always
            : restart==QStringLiteral("on-failure") ? domain::services::RestartPolicy::OnFailure : domain::services::RestartPolicy::Never;
        item.stopTimeoutMs=query.value(10).toInt(); definitions.append(std::move(item));
    }
    return shared::common::Result<QVector<Definition>>::success(std::move(definitions));
}
shared::common::Result<void> SqlServiceDefinitionRepository::save(const Definition& item) {
    if (QThread::currentThread()!=m_database.thread() || !m_database.database().isOpen()) return dbFailure(QSqlError(QStringLiteral("database unavailable")), QStringLiteral("save service"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO service_definitions(id,workspace_id,display_name,executable_path,arguments_json,working_directory,"
        "shell_mode,environment_names_json,expected_ports_json,restart_policy,stop_timeout_ms) "
        "SELECT ?,?,?,?,?,?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=?) "
        "ON CONFLICT(id) DO UPDATE SET display_name=excluded.display_name,executable_path=excluded.executable_path,arguments_json=excluded.arguments_json,"
        "working_directory=excluded.working_directory,shell_mode=excluded.shell_mode,environment_names_json=excluded.environment_names_json,"
        "expected_ports_json=excluded.expected_ports_json,restart_policy=excluded.restart_policy,stop_timeout_ms=excluded.stop_timeout_ms,"
        "updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE service_definitions.workspace_id=excluded.workspace_id "
        "AND EXISTS(SELECT 1 FROM workspaces WHERE id=excluded.workspace_id AND account_id=?)"));
    const QStringList values{item.id,item.workspaceId,item.name,item.program,jsonStrings(item.arguments),item.workingDirectory,
        item.shellMode?QStringLiteral("1"):QStringLiteral("0"),jsonStrings(item.environmentNames),jsonPorts(item.expectedPorts),restartName(item.restartPolicy),QString::number(item.stopTimeoutMs)};
    for (const auto& value : values) query.addBindValue(value);
    query.addBindValue(item.workspaceId); query.addBindValue(m_accountId.toString()); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return dbFailure(query.lastError(), QStringLiteral("save service"));
    if (query.numRowsAffected()==0) return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Permission,
        QStringLiteral("The service does not belong to this workspace or account."), QStringLiteral("Account-scoped upsert matched no workspace."), QStringLiteral("save service")));
    return shared::common::Result<void>::success();
}
shared::common::Result<void> SqlServiceDefinitionRepository::remove(const QString& id, const QString& workspaceId) {
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("DELETE FROM service_definitions WHERE id=? AND workspace_id=? AND workspace_id IN (SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(id); query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return dbFailure(query.lastError(), QStringLiteral("remove service"));
    return shared::common::Result<void>::success();
}
}
