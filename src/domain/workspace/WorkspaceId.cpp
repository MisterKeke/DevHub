#include "WorkspaceId.h"

namespace devhub::domain::workspace {

shared::common::Result<WorkspaceId> WorkspaceId::create(shared::identifiers::IIdGenerator& generator)
{
    const QUuid value = generator.create();
    if (value.isNull()) return fromString({});
    return shared::common::Result<WorkspaceId>::success(WorkspaceId(value));
}

shared::common::Result<WorkspaceId> WorkspaceId::fromString(const QString& value)
{
    const QUuid id = QUuid::fromString(value);
    if (id.isNull()) {
        return shared::common::Result<WorkspaceId>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("The workspace could not be identified."),
            QStringLiteral("A workspace identifier must be a non-null UUID."),
            QStringLiteral("validate workspace identifier")));
    }
    return shared::common::Result<WorkspaceId>::success(WorkspaceId(id));
}

} // namespace devhub::domain::workspace
