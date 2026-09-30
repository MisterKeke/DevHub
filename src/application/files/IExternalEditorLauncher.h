#pragma once

#include "../../shared/common/Result.h"

namespace devhub::application::files {

class IExternalEditorLauncher
{
public:
    virtual ~IExternalEditorLauncher() = default;
    [[nodiscard]] virtual shared::common::Result<void> launch(const QString& program, const QString& path) = 0;
};

} // namespace devhub::application::files
