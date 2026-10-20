#pragma once

#include "../../domain/workspace/Workspace.h"
#include "../../shared/common/Result.h"

#include <QVector>

namespace devhub::application::workspaces {

class IWorkspaceRepository
{
public:
    virtual ~IWorkspaceRepository() = default;
    [[nodiscard]] virtual shared::common::Result<QVector<domain::workspace::Workspace>>
    listForAccount(const domain::account::AccountId& accountId) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<domain::workspace::Workspace>>
    listArchivedForAccount(const domain::account::AccountId& accountId) = 0;
    [[nodiscard]] virtual shared::common::Result<domain::workspace::Workspace>
    add(const domain::workspace::Workspace& workspace) = 0;
    [[nodiscard]] virtual shared::common::Result<void> rename(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId, const QString& name) = 0;
    [[nodiscard]] virtual shared::common::Result<void> setFavorite(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId, bool favorite) = 0;
    [[nodiscard]] virtual shared::common::Result<void> updateLocation(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId, const domain::workspace::WorkspacePath& path) = 0;
    [[nodiscard]] virtual shared::common::Result<void> archive(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> restore(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> recordOpened(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId, const QDateTime& timestampUtc) = 0;
    [[nodiscard]] virtual shared::common::Result<void> setRecentOrder(const domain::account::AccountId& accountId,
        const QVector<domain::workspace::WorkspaceId>& orderedIds) = 0;
};

} // namespace devhub::application::workspaces
