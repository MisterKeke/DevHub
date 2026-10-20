#pragma once

#include "../../application/workspaces/IWorkspaceRepository.h"
#include "../database/InternalDatabaseManager.h"

namespace devhub::infrastructure::workspace {

class SqlWorkspaceRepository final : public application::workspaces::IWorkspaceRepository
{
public:
    explicit SqlWorkspaceRepository(database::InternalDatabaseManager& database) : m_database(database) {}
    [[nodiscard]] shared::common::Result<QVector<domain::workspace::Workspace>>
    listForAccount(const domain::account::AccountId& accountId) override;
    [[nodiscard]] shared::common::Result<QVector<domain::workspace::Workspace>>
    listArchivedForAccount(const domain::account::AccountId& accountId) override;
    [[nodiscard]] shared::common::Result<domain::workspace::Workspace>
    add(const domain::workspace::Workspace& workspace) override;
    [[nodiscard]] shared::common::Result<void> rename(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId, const QString& name) override;
    [[nodiscard]] shared::common::Result<void> setFavorite(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId, bool favorite) override;
    [[nodiscard]] shared::common::Result<void> updateLocation(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId, const domain::workspace::WorkspacePath& path) override;
    [[nodiscard]] shared::common::Result<void> archive(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId) override;
    [[nodiscard]] shared::common::Result<void> restore(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId) override;
    [[nodiscard]] shared::common::Result<void> recordOpened(const domain::workspace::WorkspaceId& id,
        const domain::account::AccountId& accountId, const QDateTime& timestampUtc) override;
    [[nodiscard]] shared::common::Result<void> setRecentOrder(const domain::account::AccountId& accountId,
        const QVector<domain::workspace::WorkspaceId>& orderedIds) override;

private:
    [[nodiscard]] shared::common::Result<void> ensureReady(const QString& operation) const;
    database::InternalDatabaseManager& m_database;
};

} // namespace devhub::infrastructure::workspace
