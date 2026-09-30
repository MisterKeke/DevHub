#pragma once

#include "PortTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::ports {

class IProcessInspector
{
public:
    virtual ~IProcessInspector() = default;
    [[nodiscard]] virtual shared::common::Result<ProcessDetails> inspect(quint32 processId) = 0;
};

} // namespace devhub::application::ports
