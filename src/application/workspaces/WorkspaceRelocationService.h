#pragma once

#include "IWorkspaceRepository.h"
#include "WorkspaceValidation.h"

namespace devhub::application::workspaces {

class WorkspaceRelocationService final
{
public:
    WorkspaceRelocationService(IWorkspaceRepository& repository, const WorkspaceValidator& validator,
                               domain::account::AccountId accountId)
        : m_repository(repository), m_validator(validator), m_accountId(std::move(accountId)) {}
    [[nodiscard]] shared::common::Result<domain::workspace::WorkspacePath> locate(
        const domain::workspace::WorkspaceId& id, const QString& replacementPath);

private:
    IWorkspaceRepository& m_repository;
    const WorkspaceValidator& m_validator;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::application::workspaces
