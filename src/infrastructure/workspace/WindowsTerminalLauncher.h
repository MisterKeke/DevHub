#pragma once
#include "../../application/workspaces/ITerminalLauncher.h"

namespace devhub::application::settings { class SettingsService; }
namespace devhub::infrastructure::workspace {
class WindowsTerminalLauncher final : public application::workspaces::ITerminalLauncher {
public:
    explicit WindowsTerminalLauncher(application::settings::SettingsService& settings) : m_settings(settings) {}
    [[nodiscard]] shared::common::Result<void> openAt(const QString& directory) override;
private:
    application::settings::SettingsService& m_settings;
};
} // namespace devhub::infrastructure::workspace
