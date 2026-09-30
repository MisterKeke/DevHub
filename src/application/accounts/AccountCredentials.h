#pragma once

#include "../../domain/account/Account.h"
#include "../../domain/account/PasswordVerifier.h"

#include <utility>

namespace devhub::application::accounts {

struct AccountCredentials final
{
    domain::account::Account account;
    domain::account::PasswordVerifier passwordVerifier;

    AccountCredentials(domain::account::Account accountValue,
                       domain::account::PasswordVerifier passwordVerifierValue)
        : account(std::move(accountValue)), passwordVerifier(std::move(passwordVerifierValue))
    {
    }
    AccountCredentials(const AccountCredentials&) = delete;
    AccountCredentials& operator=(const AccountCredentials&) = delete;
    AccountCredentials(AccountCredentials&&) noexcept = default;
    AccountCredentials& operator=(AccountCredentials&&) noexcept = default;
};

} // namespace devhub::application::accounts
