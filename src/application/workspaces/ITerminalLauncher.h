#pragma once

#include "../../shared/common/Result.h"

namespace devhub::application::workspaces {
class ITerminalLauncher
{
public:
    virtual ~ITerminalLauncher() = default;
    [[nodiscard]] virtual shared::common::Result<void> openAt(const QString& directory) = 0;
};
}
