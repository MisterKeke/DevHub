#include "SqlNotificationRepository.h"
#include "../database/SqlErrorMapper.h"

#include <QSqlQuery>

namespace devhub::infrastructure::notifications {
using application::notifications::Notification;
using application::notifications::INotificationRepository;
using database::SqlErrorMapper;

shared::common::Result<void> SqlNotificationRepository::add(const Notification& notification)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO notifications(id,severity,category,title,message,deduplication_key,created_at_utc,workspace_id) "
        "VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(deduplication_key) DO UPDATE SET "
        "repeat_count=repeat_count+1,created_at_utc=excluded.created_at_utc,title=excluded.title,message=excluded.message"));
    query.addBindValue(notification.id.toString(QUuid::WithoutBraces));
    query.addBindValue(static_cast<int>(notification.severity));
    query.addBindValue(static_cast<int>(notification.category));
    query.addBindValue(m_redactor.redact(notification.title));
    query.addBindValue(m_redactor.redact(notification.message));
    query.addBindValue(notification.deduplicationKey);
    query.addBindValue(notification.createdAtUtc.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(notification.workspaceId ? notification.workspaceId->toString(QUuid::WithoutBraces) : QVariant{});
    if (!query.exec()) return shared::common::Result<void>::failure(
        SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("persist notification")));
    return shared::common::Result<void>::success();
}

shared::common::Result<QVector<Notification>> SqlNotificationRepository::list(
    const QString& workspaceId, int severity, int category, bool unreadOnly)
{
    QString sql = QStringLiteral("SELECT id,severity,category,title,message,deduplication_key,created_at_utc,workspace_id,is_read,repeat_count "
                                 "FROM notifications WHERE 1=1");
    if (!workspaceId.isEmpty()) sql += QStringLiteral(" AND workspace_id=?");
    if (severity >= 0) sql += QStringLiteral(" AND severity=?");
    if (category >= 0) sql += QStringLiteral(" AND category=?");
    if (unreadOnly) sql += QStringLiteral(" AND is_read=0");
    sql += QStringLiteral(" ORDER BY created_at_utc DESC LIMIT 500");
    QSqlQuery query(m_database.database()); query.prepare(sql);
    if (!workspaceId.isEmpty()) query.addBindValue(workspaceId);
    if (severity >= 0) query.addBindValue(severity);
    if (category >= 0) query.addBindValue(category);
    if (!query.exec()) return shared::common::Result<QVector<Notification>>::failure(
        SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("list notifications")));
    QVector<Notification> values;
    while (query.next()) {
        Notification item;
        item.id = QUuid::fromString(query.value(0).toString());
        item.severity = static_cast<application::notifications::NotificationSeverity>(query.value(1).toInt());
        item.category = static_cast<application::notifications::NotificationCategory>(query.value(2).toInt());
        item.title = query.value(3).toString(); item.message = query.value(4).toString();
        item.deduplicationKey = query.value(5).toString();
        item.createdAtUtc = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs).toUTC();
        if (!query.value(7).isNull()) item.workspaceId = QUuid::fromString(query.value(7).toString());
        item.isRead = query.value(8).toInt() != 0;
        item.repeatCount = query.value(9).toInt();
        values.push_back(std::move(item));
    }
    return shared::common::Result<QVector<Notification>>::success(std::move(values));
}

shared::common::Result<void> SqlNotificationRepository::setRead(const QUuid& id, bool read)
{
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("UPDATE notifications SET is_read=? WHERE id=?"));
    query.addBindValue(read ? 1 : 0); query.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!query.exec()) return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("update notification state")));
    return shared::common::Result<void>::success();
}
shared::common::Result<void> SqlNotificationRepository::clearRead()
{
    QSqlQuery query(m_database.database());
    if (!query.exec(QStringLiteral("DELETE FROM notifications WHERE is_read=1")))
        return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("clear read notifications")));
    return shared::common::Result<void>::success();
}
shared::common::Result<void> SqlNotificationRepository::prune(int retentionDays)
{
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("DELETE FROM notifications WHERE created_at_utc < ?"));
    query.addBindValue(QDateTime::currentDateTimeUtc().addDays(-qBound(1, retentionDays, 3650)).toString(Qt::ISODateWithMs));
    if (!query.exec()) return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("prune notifications")));
    return shared::common::Result<void>::success();
}
} // namespace devhub::infrastructure::notifications
