#include "SqlConversationRepository.h"

#include "../database/SqlErrorMapper.h"
#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSet>
#include <QThread>

namespace devhub::infrastructure::assistant {
using namespace application::assistant;
using shared::common::Result;
namespace {
Result<void> invalidMetadata(const QString& detail = QStringLiteral("Conversation metadata failed validation."))
{
    return Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::DatabaseQueryFailed, domain::errors::ErrorCategory::Persistence,
        QStringLiteral("The conversation could not be saved."), detail,
        QStringLiteral("save assistant conversation")));
}
QString utc(const QDateTime& value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}
bool safeMetadataText(const QString& value, qsizetype maximum)
{
    if (value.size() > maximum) return false;
    for (const QChar ch : value) if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
}

SqlConversationRepository::SqlConversationRepository(database::InternalDatabaseManager& database,
    domain::account::AccountId accountId)
    : m_database(database), m_accountId(std::move(accountId)) {}

Result<QList<AssistantConversationMetadata>> SqlConversationRepository::list(const QString& accountId, int limit)
{
    if (QThread::currentThread() != m_database.thread() || accountId != m_accountId.toString())
        return Result<QList<AssistantConversationMetadata>>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::DatabaseQueryFailed, domain::errors::ErrorCategory::Persistence,
            QStringLiteral("Saved conversations are unavailable for this account."), {}, QStringLiteral("list assistant conversations")));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT id,COALESCE(workspace_id,''),COALESCE(workspace_root_snapshot,''),title,"
        "created_at_utc,updated_at_utc,COALESCE(last_opened_at_utc,''),codex_thread_id,codex_account_key,codex_home_key,"
        "compatibility_profile,COALESCE(last_turn_id,''),COALESCE(last_turn_status,'') "
        "FROM assistant_conversations WHERE account_id=? ORDER BY updated_at_utc DESC,id LIMIT ?"));
    query.addBindValue(accountId); query.addBindValue(qBound(1, limit, 100));
    if (!query.exec()) return Result<QList<AssistantConversationMetadata>>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("list assistant conversations")));
    QList<AssistantConversationMetadata> values;
    while (query.next()) {
        AssistantConversationMetadata metadata;
        metadata.conversation.id = QUuid::fromString(query.value(0).toString());
        metadata.conversation.context.workspaceId = query.value(1).toString();
        metadata.conversation.context.canonicalRoot = query.value(2).toString();
        metadata.conversation.title = query.value(3).toString();
        metadata.devhubAccountId = accountId;
        metadata.createdAtUtc = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs).toUTC();
        metadata.updatedAtUtc = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs).toUTC();
        if (!query.value(6).toString().isEmpty()) metadata.lastOpenedAtUtc = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs).toUTC();
        metadata.binding.threadId = query.value(7).toString();
        metadata.binding.accountFingerprint = query.value(8).toString();
        // The home key is a digest. Keep the original home out of DevHub metadata.
        metadata.binding.codexHomeKey = query.value(9).toString();
        metadata.binding.profileFingerprint = query.value(10).toString();
        metadata.lastTurnId = query.value(11).toString(); metadata.lastTurnStatus = query.value(12).toString();
        values.append(std::move(metadata));
    }
    return Result<QList<AssistantConversationMetadata>>::success(std::move(values));
}

Result<void> SqlConversationRepository::save(const AssistantConversationMetadata& metadata)
{
    const auto& binding = metadata.binding;
    if (QThread::currentThread() != m_database.thread() || metadata.devhubAccountId != m_accountId.toString()
        || metadata.conversation.id.isNull() || binding.threadId.trimmed().isEmpty()
        || !safeMetadataText(binding.threadId, 512) || binding.accountFingerprint.trimmed().isEmpty()
        || !safeMetadataText(binding.accountFingerprint, 128) || binding.codexHome.trimmed().isEmpty()
        || !safeMetadataText(binding.codexHome, 2048) || binding.profileFingerprint.trimmed().isEmpty()
        || !safeMetadataText(binding.profileFingerprint, 128) || !safeMetadataText(metadata.conversation.title, 80)
        || !safeMetadataText(metadata.conversation.context.workspaceId, 64)
        || !safeMetadataText(metadata.conversation.context.canonicalRoot, 4096)
        || !safeMetadataText(metadata.lastTurnId, 512)
        || !QSet<QString>{QString{}, QStringLiteral("accepted"), QStringLiteral("completed"),
            QStringLiteral("interrupted"), QStringLiteral("uncertain"), QStringLiteral("failed"),
            QStringLiteral("closed")}.contains(metadata.lastTurnStatus)
        || !metadata.createdAtUtc.isValid() || !metadata.updatedAtUtc.isValid()) return invalidMetadata();

    const QString homeKey = QString::fromLatin1(QCryptographicHash::hash(binding.codexHome.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString workspaceId = metadata.conversation.context.workspaceId.isEmpty()
        ? QStringLiteral("") : metadata.conversation.context.workspaceId;
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO assistant_conversations(id,account_id,workspace_id,codex_thread_id,"
        "codex_home_key,codex_account_key,workspace_root_snapshot,title,created_at_utc,updated_at_utc,last_opened_at_utc,"
        "last_turn_id,last_turn_status,compatibility_profile,tool_profile_hash) "
        "SELECT ?,?,NULLIF(?,''),?,?,?,?,?,?,?,?,?,?,?,NULL "
        "WHERE (?='' OR EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=?)) "
        "ON CONFLICT(id) DO UPDATE SET workspace_id=excluded.workspace_id,codex_thread_id=excluded.codex_thread_id,"
        "codex_home_key=excluded.codex_home_key,codex_account_key=excluded.codex_account_key,"
        "workspace_root_snapshot=excluded.workspace_root_snapshot,title=excluded.title,updated_at_utc=excluded.updated_at_utc,"
        "last_opened_at_utc=excluded.last_opened_at_utc,last_turn_id=excluded.last_turn_id,last_turn_status=excluded.last_turn_status,"
        "compatibility_profile=excluded.compatibility_profile,tool_profile_hash=excluded.tool_profile_hash "
        "WHERE assistant_conversations.account_id=excluded.account_id"));
    query.addBindValue(metadata.conversation.id.toString(QUuid::WithoutBraces)); query.addBindValue(metadata.devhubAccountId);
    query.addBindValue(workspaceId); query.addBindValue(binding.threadId); query.addBindValue(homeKey);
    query.addBindValue(binding.accountFingerprint); query.addBindValue(metadata.conversation.context.canonicalRoot);
    query.addBindValue(metadata.conversation.title.left(80)); query.addBindValue(utc(metadata.createdAtUtc));
    query.addBindValue(utc(metadata.updatedAtUtc)); query.addBindValue(metadata.lastOpenedAtUtc.isValid() ? utc(metadata.lastOpenedAtUtc) : QVariant());
    query.addBindValue(metadata.lastTurnId.isEmpty() ? QVariant() : QVariant(metadata.lastTurnId));
    query.addBindValue(metadata.lastTurnStatus.isEmpty() ? QVariant() : QVariant(metadata.lastTurnStatus));
    query.addBindValue(binding.profileFingerprint); query.addBindValue(workspaceId); query.addBindValue(workspaceId);
    query.addBindValue(metadata.devhubAccountId);
    if (!query.exec()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("save assistant conversation")));
    // INSERT ... SELECT row counts are reported inconsistently by SQLite
    // drivers. Verify the scoped row itself so a successful write is not
    // misclassified as a failed save (while still detecting rejected scope).
    QSqlQuery verify(m_database.database());
    verify.prepare(QStringLiteral("SELECT 1 FROM assistant_conversations WHERE id=? AND account_id=? LIMIT 1"));
    verify.addBindValue(metadata.conversation.id.toString(QUuid::WithoutBraces));
    verify.addBindValue(metadata.devhubAccountId);
    if (!verify.exec() || !verify.next())
        return invalidMetadata(QStringLiteral("The scoped conversation row was not saved."));
    return Result<void>::success();
}
} // namespace devhub::infrastructure::assistant
