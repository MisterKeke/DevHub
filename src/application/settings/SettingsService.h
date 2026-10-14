#pragma once
#include "ISettingsRepository.h"

namespace devhub::application::settings {
class SettingsService final {
public:
    explicit SettingsService(ISettingsRepository& repository);
    [[nodiscard]] QVariant value(const QString& id, const QString& workspaceId = {}) const;
    [[nodiscard]] SettingValues effectiveValues(const QString& workspaceId = {}) const;
    [[nodiscard]] shared::common::Result<void> setValue(const QString& id, QVariant value,
                                                         const QString& workspaceId = {});
    [[nodiscard]] shared::common::Result<void> setValues(const SettingValues& values,
                                                          const QString& workspaceId = {});
    [[nodiscard]] SettingDescriptor descriptor(const QString& id) const;
    [[nodiscard]] QVector<SettingDescriptor> descriptors() const;
    [[nodiscard]] SettingValues sanitizedValues(const QString& workspaceId = {}) const;
private:
    ISettingsRepository& m_repository;
    QVector<SettingDescriptor> m_descriptors;
    SettingValues m_sessionValues;
};
} // namespace devhub::application::settings
