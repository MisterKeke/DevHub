#pragma once
#include "../../application/workspaces/ITerminalLauncher.h"
namespace devhub::infrastructure::workspace {
class SystemTerminalLauncher final : public application::workspaces::ITerminalLauncher
{
public:
    [[nodiscard]] shared::common::Result<void> openAt(const QString& directory) override;
};
}
