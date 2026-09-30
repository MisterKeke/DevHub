#pragma once

#include "../../application/logs/ILogSourceRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../domain/account/AccountId.h"

namespace devhub::infrastructure::logs {

class SqlLogSourceRepository final : public application::logs::ILogSourceRepository
{
public:
    SqlLogSourceRepository(database::InternalDatabaseManager& database, domain::account::AccountId accountId)
        : m_database(database), m_accountId(std::move(accountId)) {}

    [[nodiscard]] shared::common::Result<QVector<application::logs::LogSourceConfig>> list(const QString& workspaceId) override;
    [[nodiscard]] shared::common::Result<void> add(const application::logs::LogSourceConfig& source) override;
    [[nodiscard]] shared::common::Result<void> remove(const QString& sourceId, const QString& workspaceId) override;

private:
    database::InternalDatabaseManager& m_database;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::infrastructure::logs
