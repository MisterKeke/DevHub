#pragma once
#include "../../application/notifications/INotificationRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../shared/security/RedactionService.h"

namespace devhub::infrastructure::notifications {
class SqlNotificationRepository final : public application::notifications::INotificationRepository {
public:
    SqlNotificationRepository(database::InternalDatabaseManager& database, const shared::security::RedactionService& redactor)
        : m_database(database), m_redactor(redactor) {}
    [[nodiscard]] shared::common::Result<void> add(const application::notifications::Notification& notification) override;
    [[nodiscard]] shared::common::Result<QVector<application::notifications::Notification>> list(
        const QString& workspaceId = {}, int severity = -1, int category = -1, bool unreadOnly = false) override;
    [[nodiscard]] shared::common::Result<void> setRead(const QUuid& id, bool read) override;
    [[nodiscard]] shared::common::Result<void> clearRead() override;
    [[nodiscard]] shared::common::Result<void> prune(int retentionDays) override;
private:
    database::InternalDatabaseManager& m_database;
    const shared::security::RedactionService& m_redactor;
};
} // namespace devhub::infrastructure::notifications
