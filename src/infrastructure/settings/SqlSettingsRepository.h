#pragma once
#include "../../application/settings/ISettingsRepository.h"
#include "../database/InternalDatabaseManager.h"

namespace devhub::infrastructure::settings {
class SqlSettingsRepository final : public application::settings::ISettingsRepository {
public:
    explicit SqlSettingsRepository(database::InternalDatabaseManager& manager) : m_manager(manager) {}
    [[nodiscard]] shared::common::Result<application::settings::SettingValues> load(
        application::settings::SettingScope scope, const QString& workspaceId = {}) override;
    [[nodiscard]] shared::common::Result<void> save(application::settings::SettingScope scope,
        const QString& workspaceId, const QString& id, const QVariant& value) override;
    [[nodiscard]] shared::common::Result<void> saveBatch(const application::settings::SettingUpdates& updates) override;
private:
    database::InternalDatabaseManager& m_manager;
};
} // namespace devhub::infrastructure::settings
