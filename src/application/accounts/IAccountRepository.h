#pragma once

#include "AccountCredentials.h"
#include "../../shared/common/Result.h"

#include <optional>
#include <utility>

namespace devhub::application::accounts {

class IAccountRepository
{
public:
    virtual ~IAccountRepository() = default;

    [[nodiscard]] virtual shared::common::Result<std::optional<domain::account::Account>>
    findSingleAccount() = 0;
    [[nodiscard]] virtual shared::common::Result<std::optional<AccountCredentials>>
    findByUsername(const domain::account::NormalizedUsername& username) = 0;
    [[nodiscard]] virtual shared::common::Result<domain::account::Account>
    createAccount(domain::account::DisplayUsername displayUsername,
                  domain::account::NormalizedUsername normalizedUsername,
                  domain::account::PasswordVerifier verifier) = 0;
    [[nodiscard]] virtual shared::common::Result<domain::account::Account>
    createAccountWithId(domain::account::AccountId id,
                        domain::account::DisplayUsername displayUsername,
                        domain::account::NormalizedUsername normalizedUsername,
                        domain::account::PasswordVerifier verifier) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    deleteAccount(const domain::account::AccountId& id) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    replacePasswordVerifier(const domain::account::AccountId& id,
                            domain::account::PasswordVerifier verifier,
                            const QDateTime& updatedAtUtc) = 0;
    [[nodiscard]] virtual shared::common::Result<void>
    recordSuccessfulLogin(const domain::account::AccountId& id,
                          const QDateTime& timestampUtc,
                          std::optional<domain::account::PasswordVerifier> replacementVerifier) = 0;
};

} // namespace devhub::application::accounts
