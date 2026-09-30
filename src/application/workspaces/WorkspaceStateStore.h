#pragma once

#include "../../domain/account/AccountId.h"
#include "../../domain/workspace/WorkspaceId.h"

#include <optional>

namespace devhub::application::workspaces {

class WorkspaceStateStore final
{
public:
    [[nodiscard]] std::optional<domain::workspace::WorkspaceId> selected(const domain::account::AccountId& accountId) const;
    void setSelected(const domain::account::AccountId& accountId, const domain::workspace::WorkspaceId& workspaceId) const;
    void clearSelected(const domain::account::AccountId& accountId) const;
};

} // namespace devhub::application::workspaces
