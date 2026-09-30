#pragma once

#include "../../application/accounts/IAccountRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../shared/identifiers/IdGenerator.h"

namespace devhub::infrastructure::account {

class SqlAccountRepository final : public application::accounts::IAccountRepository
{
public:
    SqlAccountRepository(database::InternalDatabaseManager& databaseManager,
                         shared::identifiers::IIdGenerator& idGenerator);

    [[nodiscard]] shared::common::Result<std::optional<domain::account::Account>>
    findSingleAccount() override;
    [[nodiscard]] shared::common::Result<std::optional<application::accounts::AccountCredentials>>
    findByUsername(const domain::account::NormalizedUsername& username) override;
    [[nodiscard]] shared::common::Result<domain::account::Account>
    createAccount(domain::account::DisplayUsername displayUsername,
                  domain::account::NormalizedUsername normalizedUsername,
                  domain::account::PasswordVerifier verifier) override;
    [[nodiscard]] shared::common::Result<domain::account::Account>
    createAccountWithId(domain::account::AccountId id,
                         domain::account::DisplayUsername displayUsername,
                         domain::account::NormalizedUsername normalizedUsername,
                         domain::account::PasswordVerifier verifier) override;
    [[nodiscard]] shared::common::Result<void>
    deleteAccount(const domain::account::AccountId& id) override;
    [[nodiscard]] shared::common::Result<void>
    replacePasswordVerifier(const domain::account::AccountId& id,
                            domain::account::PasswordVerifier verifier,
                            const QDateTime& updatedAtUtc) override;
    [[nodiscard]] shared::common::Result<void>
    recordSuccessfulLogin(const domain::account::AccountId& id,
                          const QDateTime& timestampUtc,
                          std::optional<domain::account::PasswordVerifier> replacementVerifier) override;

private:
    [[nodiscard]] shared::common::Result<void> ensureOwnerThread(const QString& operation) const;
    [[nodiscard]] shared::common::Result<application::accounts::AccountCredentials>
    readCredentials(QSqlQuery& query) const;

    database::InternalDatabaseManager& m_databaseManager;
    shared::identifiers::IIdGenerator& m_idGenerator;
};

} // namespace devhub::infrastructure::account
