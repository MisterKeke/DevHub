#pragma once

#include "../../application/accounts/IProfileResetter.h"
#include "../database/InternalDatabaseManager.h"

#include <functional>

namespace devhub::infrastructure::filesystem {

class LocalProfileResetter final : public application::accounts::IProfileResetter
{
public:
    using MovePath = std::function<bool(const QString&, const QString&)>;
    LocalProfileResetter(QString localDataDirectory,
                         QString usersDirectory,
                         database::InternalDatabaseManager& database,
                         MovePath movePath = {});

    [[nodiscard]] shared::common::Result<void> resetProfile(
        const domain::account::AccountId& accountId) override;

private:
    QString m_localDataDirectory;
    QString m_usersDirectory;
    database::InternalDatabaseManager& m_database;
    MovePath m_movePath;
};

} // namespace devhub::infrastructure::filesystem
