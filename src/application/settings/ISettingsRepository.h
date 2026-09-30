#pragma once
#include "SettingTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::settings {
class ISettingsRepository {
public:
    virtual ~ISettingsRepository() = default;
    [[nodiscard]] virtual shared::common::Result<SettingValues> load(SettingScope scope,
                                                                      const QString& workspaceId = {}) = 0;
    [[nodiscard]] virtual shared::common::Result<void> save(SettingScope scope, const QString& workspaceId,
                                                             const QString& id, const QVariant& value) = 0;
};
} // namespace devhub::application::settings
