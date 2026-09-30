#pragma once

#include "../../domain/account/AccountId.h"
#include "../../shared/common/Result.h"

namespace devhub::application::accounts {

class IUserDirectoryProvisioner
{
public:
    virtual ~IUserDirectoryProvisioner() = default;
    [[nodiscard]] virtual shared::common::Result<void> createInitialDirectories(
        const domain::account::AccountId& accountId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> removeUserDirectory(
        const domain::account::AccountId& accountId) = 0;
};

} // namespace devhub::application::accounts
