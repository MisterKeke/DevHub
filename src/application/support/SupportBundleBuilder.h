#pragma once
#include "../settings/SettingsService.h"
#include "../../shared/security/RedactionService.h"
#include "../../shared/common/Result.h"

namespace devhub::application::support {
struct SupportBundleOptions final { bool settings = true; bool logs = true; bool moduleStatus = true; bool crashInformation = false; };
class SupportBundleBuilder final {
public:
    SupportBundleBuilder(QString logDirectory, settings::SettingsService& settings,
                         const shared::security::RedactionService& redactor);
    [[nodiscard]] QStringList previewCategories(const SupportBundleOptions& options) const;
    [[nodiscard]] shared::common::Result<void> write(const QString& outputPath,
        const SupportBundleOptions& options, const QStringList& moduleStatus) const;
private:
    QString m_logDirectory;
    settings::SettingsService& m_settings;
    const shared::security::RedactionService& m_redactor;
};
} // namespace devhub::application::support
