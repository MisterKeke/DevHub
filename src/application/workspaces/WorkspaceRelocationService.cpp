#include "WorkspaceRelocationService.h"

namespace devhub::application::workspaces {

shared::common::Result<domain::workspace::WorkspacePath> WorkspaceRelocationService::locate(
    const domain::workspace::WorkspaceId& id, const QString& replacementPath)
{
    auto validated = m_validator.validate(replacementPath);
    if (!validated) return shared::common::Result<domain::workspace::WorkspacePath>::failure(validated.error());
    auto rows = m_repository.listForAccount(m_accountId);
    if (!rows) return shared::common::Result<domain::workspace::WorkspacePath>::failure(rows.error());
    for (const auto& row : rows.value()) {
        if (row.id() != id && WorkspaceValidator::pathsEqual(row.path().canonicalPath(), validated.value().path.canonicalPath())) {
            return shared::common::Result<domain::workspace::WorkspacePath>::failure(domain::errors::AppError::failure(
                domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
                QStringLiteral("That folder belongs to another registered workspace."),
                QStringLiteral("Relocation target duplicates workspace %1.").arg(row.id().toString()),
                QStringLiteral("relocate workspace")));
        }
    }
    auto updated = m_repository.updateLocation(id, m_accountId, validated.value().path);
    if (!updated) return shared::common::Result<domain::workspace::WorkspacePath>::failure(updated.error());
    return shared::common::Result<domain::workspace::WorkspacePath>::success(validated.value().path);
}

} // namespace devhub::application::workspaces
