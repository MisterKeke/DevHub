#pragma once

#include "IWorkspaceRepository.h"
#include "WorkspaceValidation.h"
#include "../../shared/identifiers/IdGenerator.h"
#include "../../shared/time/ApplicationClock.h"

namespace devhub::application::workspaces {

class WorkspaceManager final
{
public:
    WorkspaceManager(IWorkspaceRepository& repository, const WorkspaceValidator& validator,
                     shared::identifiers::IIdGenerator& ids, shared::time::IApplicationClock& clock,
                     domain::account::AccountId accountId)
        : m_repository(repository), m_validator(validator), m_ids(ids), m_clock(clock),
          m_accountId(std::move(accountId)) {}
    [[nodiscard]] shared::common::Result<QVector<domain::workspace::Workspace>> list();
    [[nodiscard]] shared::common::Result<domain::workspace::Workspace> add(const QString& displayName,
                                                                           const QString& enteredPath);
    [[nodiscard]] shared::common::Result<void> rename(const domain::workspace::WorkspaceId& id, const QString& name);
    [[nodiscard]] shared::common::Result<void> setFavorite(const domain::workspace::WorkspaceId& id, bool favorite);
    [[nodiscard]] shared::common::Result<void> removeMetadata(const domain::workspace::WorkspaceId& id);
    [[nodiscard]] shared::common::Result<domain::workspace::Workspace> activate(const domain::workspace::WorkspaceId& id);
    [[nodiscard]] shared::common::Result<void> reorder(const QVector<domain::workspace::WorkspaceId>& ids);
    [[nodiscard]] const domain::account::AccountId& accountId() const noexcept { return m_accountId; }
    [[nodiscard]] IWorkspaceRepository& repository() noexcept { return m_repository; }
    [[nodiscard]] const WorkspaceValidator& validator() const noexcept { return m_validator; }

private:
    IWorkspaceRepository& m_repository;
    const WorkspaceValidator& m_validator;
    shared::identifiers::IIdGenerator& m_ids;
    shared::time::IApplicationClock& m_clock;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::application::workspaces
