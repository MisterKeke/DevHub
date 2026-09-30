#pragma once

#include "../../domain/errors/AppError.h"

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace devhub::application::notifications {

enum class NotificationSeverity
{
    Information,
    Success,
    Warning,
    Error
};

enum class NotificationCategory
{
    General,
    Workspace,
    Service,
    Port,
    Git,
    Database,
    Security
};

struct Notification final
{
    QUuid id;
    NotificationSeverity severity = NotificationSeverity::Information;
    NotificationCategory category = NotificationCategory::General;
    QString title;
    QString message;
    QString deduplicationKey;
    QDateTime createdAtUtc;
    std::optional<QUuid> workspaceId;
    std::optional<domain::errors::CorrelationId> correlationId;
    bool isRead = false;
    int repeatCount = 1;
};

struct NotificationDraft final
{
    NotificationSeverity severity = NotificationSeverity::Information;
    NotificationCategory category = NotificationCategory::General;
    QString title;
    QString message;
    QString deduplicationKey;
    std::optional<QUuid> workspaceId;
    std::optional<domain::errors::CorrelationId> correlationId;
};

} // namespace devhub::application::notifications

Q_DECLARE_METATYPE(devhub::application::notifications::Notification)
