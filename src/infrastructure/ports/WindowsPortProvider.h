#pragma once

#include "../../application/ports/IPortProvider.h"

namespace devhub::infrastructure::ports {

class WindowsPortProvider final : public application::ports::IPortProvider
{
public:
    [[nodiscard]] shared::common::Result<QVector<application::ports::PortRecord>> enumerate() override;
};

} // namespace devhub::infrastructure::ports
