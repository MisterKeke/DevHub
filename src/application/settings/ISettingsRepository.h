#pragma once
#include "SettingTypes.h"
#include "../../shared/common/Result.h"
#include "../../domain/errors/AppError.h"

namespace devhub::application::settings {
class ISettingsRepository {
public:
    virtual ~ISettingsRepository() = default;
    [[nodiscard]] virtual shared::common::Result<SettingValues> load(SettingScope scope,
                                                                      const QString& workspaceId = {}) = 0;
    [[nodiscard]] virtual shared::common::Result<void> save(SettingScope scope, const QString& workspaceId,
                                                             const QString& id, const QVariant& value) = 0;
    [[nodiscard]] virtual shared::common::Result<void> saveBatch(const SettingUpdates& updates)
    {
        if (updates.isEmpty()) return shared::common::Result<void>::success();
        if (updates.size() == 1) {
            const auto& item = updates.first();
            return save(item.scope, item.workspaceId, item.id, item.value);
        }
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Persistence,
            QStringLiteral("Settings could not be saved as a single change."),
            QStringLiteral("This settings repository does not support atomic batch writes."),
            QStringLiteral("save settings batch")));
    }
};
} // namespace devhub::application::settings
