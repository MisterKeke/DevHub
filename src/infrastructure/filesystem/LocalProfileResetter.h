#pragma once

#include "../../application/accounts/IProfileResetter.h"
#include "../database/InternalDatabaseManager.h"

namespace devhub::infrastructure::filesystem {

class LocalProfileResetter final : public application::accounts::IProfileResetter
{
public:
    LocalProfileResetter(QString localDataDirectory,
                         QString usersDirectory,
                         database::InternalDatabaseManager& database);

    [[nodiscard]] shared::common::Result<void> resetProfile(
        const domain::account::AccountId& accountId) override;

private:
    QString m_localDataDirectory;
    QString m_usersDirectory;
    database::InternalDatabaseManager& m_database;
};

} // namespace devhub::infrastructure::filesystem
