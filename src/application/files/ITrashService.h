#pragma once

#include "../../shared/common/Result.h"

namespace devhub::application::files {

class ITrashService
{
public:
    virtual ~ITrashService() = default;
    [[nodiscard]] virtual shared::common::Result<void> moveToTrash(const QString& path) = 0;
};

} // namespace devhub::application::files
