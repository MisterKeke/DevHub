#pragma once
#include "../../application/services/IServiceDefinitionRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../domain/account/AccountId.h"

namespace devhub::infrastructure::services {
class SqlServiceDefinitionRepository final : public application::services::IServiceDefinitionRepository {
public:
    SqlServiceDefinitionRepository(database::InternalDatabaseManager& database, domain::account::AccountId accountId)
        : m_database(database), m_accountId(std::move(accountId)) {}
    shared::common::Result<QVector<domain::services::ServiceDefinition>> list(const QString& workspaceId) override;
    shared::common::Result<void> save(const domain::services::ServiceDefinition& definition) override;
    shared::common::Result<void> remove(const QString& id, const QString& workspaceId) override;
private:
    database::InternalDatabaseManager& m_database;
    domain::account::AccountId m_accountId;
};
}
