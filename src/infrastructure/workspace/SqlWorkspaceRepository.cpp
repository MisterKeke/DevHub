#include "SqlWorkspaceRepository.h"

#include "../database/SqlErrorMapper.h"
#include "../database/SqlTransactionGuard.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

namespace devhub::infrastructure::workspace {
namespace {
using Workspace = domain::workspace::Workspace;
using WorkspaceStatus = domain::workspace::WorkspaceStatus;
using AppError = domain::errors::AppError;

shared::common::Result<void> failure(const QString& message, const QString& details, const QString& operation)
{
    return shared::common::Result<void>::failure(AppError::failure(domain::errors::ErrorCode::DatabaseQueryFailed,
        domain::errors::ErrorCategory::Workspace, message, details, operation));
}

shared::common::Result<Workspace> readWorkspace(QSqlQuery& query)
{
    auto id = domain::workspace::WorkspaceId::fromString(query.value(0).toString());
    auto accountId = domain::account::AccountId::fromString(query.value(1).toString());
    const QString enteredPath = query.value(3).toString();
    const QString canonicalPath = query.value(4).toString();
    const QDateTime created = QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs).toUTC();
    const QString lastOpenedText = query.value(9).toString();
    std::optional<QDateTime> lastOpened;
    if (!lastOpenedText.isEmpty()) lastOpened = QDateTime::fromString(lastOpenedText, Qt::ISODateWithMs).toUTC();
    const QString archivedText = query.value(10).toString();
    std::optional<QDateTime> archivedAt;
    if (!archivedText.isEmpty()) archivedAt = QDateTime::fromString(archivedText, Qt::ISODateWithMs).toUTC();
    if (!id || !accountId || query.value(2).toString().trimmed().isEmpty() || enteredPath.isEmpty()
        || canonicalPath.isEmpty() || !created.isValid() || (lastOpened && !lastOpened->isValid())
        || (archivedAt && !archivedAt->isValid())) {
        return shared::common::Result<Workspace>::failure(AppError::failure(
            domain::errors::ErrorCode::DatabaseCorrupt, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("DevHub found invalid workspace details."),
            QStringLiteral("Workspace database row failed identifier, path, name, or timestamp validation."),
            QStringLiteral("read workspace")));
    }
    return shared::common::Result<Workspace>::success(Workspace(
        std::move(id).value(), std::move(accountId).value(), query.value(2).toString(),
        domain::workspace::WorkspacePath(enteredPath, canonicalPath), WorkspaceStatus::Ready,
        query.value(5).toInt() != 0, query.value(6).toInt(), created, lastOpened, archivedAt));
}

QString timestampText(const QDateTime& value) { return value.toUTC().toString(Qt::ISODateWithMs); }
const QString columns = QStringLiteral("id, account_id, display_name, entered_path, canonical_root_path, "
                                       "is_favorite, recent_order, root_path, created_at_utc, last_opened_at_utc, archived_at_utc");
}

shared::common::Result<void> SqlWorkspaceRepository::ensureReady(const QString& operation) const
{
    if (QThread::currentThread() == m_database.thread() && m_database.database().isOpen())
        return shared::common::Result<void>::success();
    return failure(QStringLiteral("DevHub could not access workspace storage."),
        QStringLiteral("Workspace SQL must run on the open database's owning thread."), operation);
}

shared::common::Result<QVector<Workspace>> SqlWorkspaceRepository::listForAccount(const domain::account::AccountId& accountId)
{
    const QString operation = QStringLiteral("list workspaces");
    auto ready = ensureReady(operation);
    if (!ready) return shared::common::Result<QVector<Workspace>>::failure(ready.error());
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT %1 FROM workspaces WHERE account_id = ? AND archived_at_utc IS NULL "
                                 "ORDER BY is_favorite DESC, recent_order ASC, display_name COLLATE NOCASE ASC")
                      .arg(columns));
    query.addBindValue(accountId.toString());
    if (!query.exec()) return shared::common::Result<QVector<Workspace>>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), operation));
    QVector<Workspace> rows;
    while (query.next()) {
        auto row = readWorkspace(query);
        if (!row) return shared::common::Result<QVector<Workspace>>::failure(row.error());
        rows.append(std::move(row).value());
    }
    return shared::common::Result<QVector<Workspace>>::success(std::move(rows));
}

shared::common::Result<QVector<Workspace>> SqlWorkspaceRepository::listArchivedForAccount(const domain::account::AccountId& accountId)
{
    const QString operation = QStringLiteral("list archived workspaces");
    auto ready = ensureReady(operation);
    if (!ready) return shared::common::Result<QVector<Workspace>>::failure(ready.error());
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT %1 FROM workspaces WHERE account_id = ? AND archived_at_utc IS NOT NULL "
                                 "ORDER BY archived_at_utc DESC, display_name COLLATE NOCASE ASC").arg(columns));
    query.addBindValue(accountId.toString());
    if (!query.exec()) return shared::common::Result<QVector<Workspace>>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), operation));
    QVector<Workspace> rows;
    while (query.next()) {
        auto row = readWorkspace(query);
        if (!row) return shared::common::Result<QVector<Workspace>>::failure(row.error());
        rows.append(std::move(row).value());
    }
    return shared::common::Result<QVector<Workspace>>::success(std::move(rows));
}

shared::common::Result<Workspace> SqlWorkspaceRepository::add(const Workspace& workspace)
{
    const QString operation = QStringLiteral("register workspace");
    auto ready = ensureReady(operation);
    if (!ready) return shared::common::Result<Workspace>::failure(ready.error());
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO workspaces(id, account_id, display_name, root_path, entered_path, "
                                 "canonical_root_path, is_favorite, recent_order) VALUES(?, ?, ?, ?, ?, ?, ?, ?)") );
    query.addBindValue(workspace.id().toString());
    query.addBindValue(workspace.accountId().toString());
    query.addBindValue(workspace.displayName());
    query.addBindValue(workspace.path().enteredPath());
    query.addBindValue(workspace.path().enteredPath());
    query.addBindValue(workspace.path().canonicalPath());
    query.addBindValue(workspace.isFavorite() ? 1 : 0);
    query.addBindValue(workspace.recentOrder());
    if (!query.exec()) return shared::common::Result<Workspace>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), operation));
    return shared::common::Result<Workspace>::success(workspace);
}

shared::common::Result<void> SqlWorkspaceRepository::rename(const domain::workspace::WorkspaceId& id,
    const domain::account::AccountId& accountId, const QString& name)
{
    auto ready = ensureReady(QStringLiteral("rename workspace")); if (!ready) return ready;
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("UPDATE workspaces SET display_name=?, updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND account_id=? AND archived_at_utc IS NULL"));
    query.addBindValue(name); query.addBindValue(id.toString()); query.addBindValue(accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("rename workspace")));
    if (query.numRowsAffected() != 1) return failure(QStringLiteral("The workspace no longer exists."), QStringLiteral("Rename matched no account-owned workspace."), QStringLiteral("rename workspace"));
    return shared::common::Result<void>::success();
}

shared::common::Result<void> SqlWorkspaceRepository::setFavorite(const domain::workspace::WorkspaceId& id,
    const domain::account::AccountId& accountId, bool favorite)
{
    auto ready = ensureReady(QStringLiteral("update workspace favorite")); if (!ready) return ready;
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("UPDATE workspaces SET is_favorite=?, updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND account_id=? AND archived_at_utc IS NULL"));
    query.addBindValue(favorite ? 1 : 0); query.addBindValue(id.toString()); query.addBindValue(accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("update workspace favorite")));
    if (query.numRowsAffected() != 1) return failure(QStringLiteral("The workspace no longer exists."), QStringLiteral("Favorite update matched no account-owned workspace."), QStringLiteral("update workspace favorite"));
    return shared::common::Result<void>::success();
}

shared::common::Result<void> SqlWorkspaceRepository::updateLocation(const domain::workspace::WorkspaceId& id,
    const domain::account::AccountId& accountId, const domain::workspace::WorkspacePath& path)
{
    auto ready = ensureReady(QStringLiteral("relocate workspace")); if (!ready) return ready;
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("UPDATE workspaces SET root_path=?, entered_path=?, canonical_root_path=?, updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND account_id=? AND archived_at_utc IS NULL"));
    query.addBindValue(path.enteredPath()); query.addBindValue(path.enteredPath()); query.addBindValue(path.canonicalPath());
    query.addBindValue(id.toString()); query.addBindValue(accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("relocate workspace")));
    if (query.numRowsAffected() != 1) return failure(QStringLiteral("The workspace no longer exists."), QStringLiteral("Location update matched no account-owned workspace."), QStringLiteral("relocate workspace"));
    return shared::common::Result<void>::success();
}

shared::common::Result<void> SqlWorkspaceRepository::archive(const domain::workspace::WorkspaceId& id,
    const domain::account::AccountId& accountId)
{
    auto ready = ensureReady(QStringLiteral("archive workspace")); if (!ready) return ready;
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("UPDATE workspaces SET archived_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now'), updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND account_id=? AND archived_at_utc IS NULL"));
    query.addBindValue(id.toString()); query.addBindValue(accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("archive workspace")));
    if (query.numRowsAffected() != 1) return failure(QStringLiteral("The workspace is no longer available to remove."), QStringLiteral("Archive matched no active account-owned workspace."), QStringLiteral("archive workspace"));
    return shared::common::Result<void>::success();
}

shared::common::Result<void> SqlWorkspaceRepository::restore(const domain::workspace::WorkspaceId& id,
    const domain::account::AccountId& accountId)
{
    auto ready = ensureReady(QStringLiteral("restore workspace")); if (!ready) return ready;
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("UPDATE workspaces SET archived_at_utc=NULL, updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND account_id=? AND archived_at_utc IS NOT NULL"));
    query.addBindValue(id.toString()); query.addBindValue(accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("restore workspace")));
    if (query.numRowsAffected() != 1) return failure(QStringLiteral("The archived workspace could not be restored."), QStringLiteral("Restore matched no archived workspace owned by the active account."), QStringLiteral("restore workspace"));
    return shared::common::Result<void>::success();
}

shared::common::Result<void> SqlWorkspaceRepository::recordOpened(const domain::workspace::WorkspaceId& id,
    const domain::account::AccountId& accountId, const QDateTime& timestampUtc)
{
    auto ready = ensureReady(QStringLiteral("record workspace activation")); if (!ready) return ready;
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return shared::common::Result<void>::failure(transaction.error().value());
    QSqlQuery bump(m_database.database());
    bump.prepare(QStringLiteral("UPDATE workspaces SET recent_order=recent_order+1 WHERE account_id=? AND id<>? AND archived_at_utc IS NULL"));
    bump.addBindValue(accountId.toString()); bump.addBindValue(id.toString());
    if (!bump.exec()) return shared::common::Result<void>::failure(
        database::SqlErrorMapper::fromSqlError(bump.lastError(), QStringLiteral("update workspace recency")));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("UPDATE workspaces SET last_opened_at_utc=?, updated_at_utc=?, recent_order=0 WHERE id=? AND account_id=? AND archived_at_utc IS NULL"));
    query.addBindValue(timestampText(timestampUtc)); query.addBindValue(timestampText(timestampUtc));
    query.addBindValue(id.toString()); query.addBindValue(accountId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("record workspace activation")));
    if (query.numRowsAffected() != 1) return failure(QStringLiteral("The workspace could not be opened."), QStringLiteral("Activation timestamp update matched no account-owned workspace."), QStringLiteral("record workspace activation"));
    return transaction.commit();
}

shared::common::Result<void> SqlWorkspaceRepository::setRecentOrder(const domain::account::AccountId& accountId,
    const QVector<domain::workspace::WorkspaceId>& orderedIds)
{
    auto ready = ensureReady(QStringLiteral("order workspaces")); if (!ready) return ready;
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return shared::common::Result<void>::failure(transaction.error().value());
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("UPDATE workspaces SET recent_order=? WHERE id=? AND account_id=? AND archived_at_utc IS NULL"));
    for (qsizetype i = 0; i < orderedIds.size(); ++i) {
        query.bindValue(0, static_cast<int>(i)); query.bindValue(1, orderedIds.at(i).toString());
        query.bindValue(2, accountId.toString());
        if (!query.exec() || query.numRowsAffected() != 1) {
            return shared::common::Result<void>::failure(query.lastError().isValid()
                ? database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("order workspaces"))
                : domain::errors::AppError::failure(domain::errors::ErrorCode::DatabaseQueryFailed,
                    domain::errors::ErrorCategory::Workspace, QStringLiteral("Workspace order could not be saved."),
                    QStringLiteral("An ordered workspace was not owned by the active account."), QStringLiteral("order workspaces")));
        }
    }
    return transaction.commit();
}

} // namespace devhub::infrastructure::workspace
