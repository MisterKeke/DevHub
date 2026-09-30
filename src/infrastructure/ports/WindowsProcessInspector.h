#pragma once

#include "../../application/ports/IProcessInspector.h"

namespace devhub::infrastructure::ports {

class WindowsProcessInspector final : public application::ports::IProcessInspector
{
public:
    [[nodiscard]] shared::common::Result<application::ports::ProcessDetails> inspect(quint32 processId) override;
};

} // namespace devhub::infrastructure::ports
