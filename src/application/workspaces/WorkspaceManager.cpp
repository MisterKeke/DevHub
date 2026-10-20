#include "WorkspaceManager.h"

#include <algorithm>

namespace devhub::application::workspaces {
namespace {
shared::common::Result<void> badName()
{
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
        QStringLiteral("Enter a workspace name."), QStringLiteral("Workspace display name is empty."),
        QStringLiteral("validate workspace name")));
}
}

shared::common::Result<QVector<domain::workspace::Workspace>> WorkspaceManager::list()
{
    auto result = m_repository.listForAccount(m_accountId);
    if (!result) return result;
    for (auto& workspace : result.value()) {
        const auto status = m_validator.inspect(workspace.path().enteredPath());
        auto path = workspace.path();
        if (status == domain::workspace::WorkspaceStatus::Ready) {
            auto canonical = m_validator.validate(workspace.path().enteredPath());
            if (canonical) path = canonical.value().path;
        }
        workspace = domain::workspace::Workspace(workspace.id(), workspace.accountId(), workspace.displayName(),
            std::move(path), status, workspace.isFavorite(), workspace.recentOrder(), workspace.createdAtUtc(),
            workspace.lastOpenedAtUtc(), workspace.archivedAtUtc());
    }
    return result;
}

shared::common::Result<QVector<domain::workspace::Workspace>> WorkspaceManager::listArchived()
{
    auto result = m_repository.listArchivedForAccount(m_accountId);
    if (!result) return result;
    for (auto& workspace : result.value()) {
        const auto status = m_validator.inspect(workspace.path().enteredPath());
        auto path = workspace.path();
        if (status == domain::workspace::WorkspaceStatus::Ready) {
            auto canonical = m_validator.validate(workspace.path().enteredPath());
            if (canonical) path = canonical.value().path;
        }
        workspace = domain::workspace::Workspace(workspace.id(), workspace.accountId(), workspace.displayName(),
            std::move(path), status, workspace.isFavorite(), workspace.recentOrder(), workspace.createdAtUtc(),
            workspace.lastOpenedAtUtc(), workspace.archivedAtUtc());
    }
    return result;
}

shared::common::Result<domain::workspace::Workspace> WorkspaceManager::add(const QString& displayName,
                                                                            const QString& enteredPath)
{
    const QString name = displayName.trimmed();
    if (name.isEmpty()) return shared::common::Result<domain::workspace::Workspace>::failure(badName().error());
    auto path = m_validator.validate(enteredPath);
    if (!path) return shared::common::Result<domain::workspace::Workspace>::failure(path.error());
    auto existing = m_repository.listForAccount(m_accountId);
    if (!existing) return shared::common::Result<domain::workspace::Workspace>::failure(existing.error());
    for (const auto& workspace : existing.value()) {
        if (WorkspaceValidator::pathsEqual(workspace.path().canonicalPath(), path.value().path.canonicalPath())) {
            return shared::common::Result<domain::workspace::Workspace>::failure(domain::errors::AppError::failure(
                domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
                QStringLiteral("That folder is already registered."),
                QStringLiteral("Duplicate canonical workspace root: %1").arg(path.value().path.canonicalPath()),
                QStringLiteral("register workspace")));
        }
    }
    auto archived = m_repository.listArchivedForAccount(m_accountId);
    if (!archived) return shared::common::Result<domain::workspace::Workspace>::failure(archived.error());
    for (const auto& workspace : archived.value()) {
        if (WorkspaceValidator::pathsEqual(workspace.path().canonicalPath(), path.value().path.canonicalPath())) {
            auto restored = m_repository.restore(workspace.id(), m_accountId);
            if (!restored) return shared::common::Result<domain::workspace::Workspace>::failure(restored.error());
            return shared::common::Result<domain::workspace::Workspace>::success(domain::workspace::Workspace(
                workspace.id(), workspace.accountId(), workspace.displayName(), path.value().path,
                domain::workspace::WorkspaceStatus::Ready, workspace.isFavorite(), workspace.recentOrder(),
                workspace.createdAtUtc(), workspace.lastOpenedAtUtc()));
        }
    }
    auto id = domain::workspace::WorkspaceId::create(m_ids);
    if (!id) return shared::common::Result<domain::workspace::Workspace>::failure(id.error());
    auto workspace = domain::workspace::Workspace(std::move(id).value(), m_accountId, name,
        path.value().path, domain::workspace::WorkspaceStatus::Ready, false,
        static_cast<int>(existing.value().size()),
        m_clock.utcNow().toUTC());
    return m_repository.add(workspace);
}

shared::common::Result<void> WorkspaceManager::rename(const domain::workspace::WorkspaceId& id, const QString& name)
{ if (name.trimmed().isEmpty()) return badName(); return m_repository.rename(id, m_accountId, name.trimmed()); }
shared::common::Result<void> WorkspaceManager::setFavorite(const domain::workspace::WorkspaceId& id, bool favorite)
{ return m_repository.setFavorite(id, m_accountId, favorite); }
shared::common::Result<void> WorkspaceManager::archive(const domain::workspace::WorkspaceId& id)
{ return m_repository.archive(id, m_accountId); }
shared::common::Result<void> WorkspaceManager::restore(const domain::workspace::WorkspaceId& id)
{
    auto archived = m_repository.listArchivedForAccount(m_accountId);
    if (!archived) return shared::common::Result<void>::failure(archived.error());
    const auto target = std::find_if(archived.value().cbegin(), archived.value().cend(), [&id](const auto& row) {
        return row.id() == id;
    });
    if (target == archived.value().cend()) return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::PathUnavailable, domain::errors::ErrorCategory::Workspace,
        QStringLiteral("That archived workspace is no longer available."),
        QStringLiteral("Restore target was not archived under the active account."), QStringLiteral("restore workspace")));
    auto active = m_repository.listForAccount(m_accountId);
    if (!active) return shared::common::Result<void>::failure(active.error());
    for (const auto& workspace : active.value()) {
        if (WorkspaceValidator::pathsEqual(workspace.path().canonicalPath(), target->path().canonicalPath()))
            return shared::common::Result<void>::failure(domain::errors::AppError::failure(
                domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
                QStringLiteral("That folder is already registered."),
                QStringLiteral("An active registration already owns this canonical workspace path."),
                QStringLiteral("restore workspace")));
    }
    return m_repository.restore(id, m_accountId);
}

shared::common::Result<domain::workspace::Workspace> WorkspaceManager::activate(const domain::workspace::WorkspaceId& id)
{
    auto rows = m_repository.listForAccount(m_accountId);
    if (!rows) return shared::common::Result<domain::workspace::Workspace>::failure(rows.error());
    for (const auto& workspace : rows.value()) {
        if (workspace.id() != id) continue;
        auto validated = m_validator.validate(workspace.path().enteredPath());
        if (!validated) return shared::common::Result<domain::workspace::Workspace>::failure(validated.error());
        const auto timestamp = m_clock.utcNow().toUTC();
        auto recorded = m_repository.recordOpened(id, m_accountId, timestamp);
        if (!recorded) return shared::common::Result<domain::workspace::Workspace>::failure(recorded.error());
        return shared::common::Result<domain::workspace::Workspace>::success(domain::workspace::Workspace(
            workspace.id(), workspace.accountId(), workspace.displayName(), validated.value().path,
            domain::workspace::WorkspaceStatus::Ready, workspace.isFavorite(), workspace.recentOrder(),
            workspace.createdAtUtc(), timestamp));
    }
    return shared::common::Result<domain::workspace::Workspace>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::PathUnavailable, domain::errors::ErrorCategory::Workspace,
        QStringLiteral("That workspace is no longer registered."), QStringLiteral("Workspace id not found for current account."),
        QStringLiteral("activate workspace")));
}

shared::common::Result<void> WorkspaceManager::reorder(const QVector<domain::workspace::WorkspaceId>& ids)
{ return m_repository.setRecentOrder(m_accountId, ids); }

} // namespace devhub::application::workspaces
