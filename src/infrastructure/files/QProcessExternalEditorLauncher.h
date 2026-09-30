#pragma once

#include "../../application/files/IExternalEditorLauncher.h"

namespace devhub::infrastructure::files {

class QProcessExternalEditorLauncher final : public application::files::IExternalEditorLauncher
{
public:
    [[nodiscard]] shared::common::Result<void> launch(const QString& program, const QString& path) override;
};

} // namespace devhub::infrastructure::files
