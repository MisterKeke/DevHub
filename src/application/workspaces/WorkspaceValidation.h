#pragma once

#include "../../domain/workspace/WorkspacePath.h"
#include "../../domain/workspace/WorkspaceStatus.h"
#include "../../shared/common/Result.h"

namespace devhub::application::workspaces {

struct ValidatedWorkspacePath final
{
    domain::workspace::WorkspacePath path;
    domain::workspace::WorkspaceStatus status = domain::workspace::WorkspaceStatus::Ready;
};

class WorkspaceValidator final
{
public:
    [[nodiscard]] shared::common::Result<ValidatedWorkspacePath> validate(const QString& enteredPath) const;
    [[nodiscard]] domain::workspace::WorkspaceStatus inspect(const QString& path) const;
    [[nodiscard]] static bool pathsEqual(const QString& left, const QString& right);
};

} // namespace devhub::application::workspaces
