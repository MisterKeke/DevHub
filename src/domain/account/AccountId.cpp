#include "AccountId.h"

#include <utility>

namespace devhub::domain::account {
namespace {

shared::common::Result<AccountId> invalidAccountId()
{
    return shared::common::Result<AccountId>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument,
        domain::errors::ErrorCategory::Authentication,
        QStringLiteral("The local account could not be identified."),
        QStringLiteral("An account identifier must be a non-null UUID."),
        QStringLiteral("validate account identifier")));
}

} // namespace

AccountId::AccountId(QUuid value)
    : m_value(std::move(value))
{
}

shared::common::Result<AccountId> AccountId::create(shared::identifiers::IIdGenerator& idGenerator)
{
    const QUuid id = idGenerator.create();
    if (id.isNull()) {
        return invalidAccountId();
    }
    return shared::common::Result<AccountId>::success(AccountId(id));
}

shared::common::Result<AccountId> AccountId::fromString(const QString& value)
{
    const QUuid id = QUuid::fromString(value);
    if (id.isNull()) {
        return invalidAccountId();
    }
    return shared::common::Result<AccountId>::success(AccountId(id));
}

} // namespace devhub::domain::account
