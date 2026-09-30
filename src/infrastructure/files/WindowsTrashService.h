#pragma once

#include "../../application/files/ITrashService.h"

namespace devhub::infrastructure::files {

class WindowsTrashService final : public application::files::ITrashService
{
public:
    [[nodiscard]] shared::common::Result<void> moveToTrash(const QString& path) override;
};

} // namespace devhub::infrastructure::files
