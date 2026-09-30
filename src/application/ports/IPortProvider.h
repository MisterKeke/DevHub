#pragma once

#include "PortTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::ports {

class IPortProvider
{
public:
    virtual ~IPortProvider() = default;
    [[nodiscard]] virtual shared::common::Result<QVector<PortRecord>> enumerate() = 0;
};

} // namespace devhub::application::ports
