#pragma once

#include "../../domain/account/AccountId.h"
#include "../../shared/common/Result.h"

namespace devhub::application::accounts {

class IProfileResetter
{
public:
    virtual ~IProfileResetter() = default;
    [[nodiscard]] virtual shared::common::Result<void> resetProfile(
        const domain::account::AccountId& accountId) = 0;
};

} // namespace devhub::application::accounts
