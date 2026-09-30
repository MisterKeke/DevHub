#pragma once
#include "Notification.h"
#include "../../shared/common/Result.h"

namespace devhub::application::notifications {
class INotificationRepository {
public:
    virtual ~INotificationRepository() = default;
    [[nodiscard]] virtual shared::common::Result<void> add(const Notification& notification) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<Notification>> list(
        const QString& workspaceId = {}, int severity = -1, int category = -1, bool unreadOnly = false) = 0;
    [[nodiscard]] virtual shared::common::Result<void> setRead(const QUuid& id, bool read) = 0;
    [[nodiscard]] virtual shared::common::Result<void> clearRead() = 0;
    [[nodiscard]] virtual shared::common::Result<void> prune(int retentionDays) = 0;
};
} // namespace devhub::application::notifications
