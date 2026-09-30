#pragma once

#include "../../application/databases/IDatabaseReferenceRepository.h"
#include "InternalDatabaseManager.h"
#include "../../domain/account/AccountId.h"

namespace devhub::infrastructure::database {

class SqlDatabaseReferenceRepository final : public application::databases::IDatabaseReferenceRepository
{
public:
    SqlDatabaseReferenceRepository(InternalDatabaseManager& database, domain::account::AccountId accountId)
        : m_database(database), m_accountId(std::move(accountId)) {}
    [[nodiscard]] shared::common::Result<QVector<application::databases::DatabaseReference>> list(const QString& workspaceId) override;
    [[nodiscard]] shared::common::Result<void> add(const application::databases::DatabaseReference& reference) override;
    [[nodiscard]] shared::common::Result<void> remove(const QString& id, const QString& workspaceId) override;

private:
    InternalDatabaseManager& m_database;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::infrastructure::database
