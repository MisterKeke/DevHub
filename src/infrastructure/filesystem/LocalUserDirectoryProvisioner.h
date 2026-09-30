#pragma once

#include "../../application/accounts/IUserDirectoryProvisioner.h"

namespace devhub::infrastructure::filesystem {

class LocalUserDirectoryProvisioner final : public application::accounts::IUserDirectoryProvisioner
{
public:
    explicit LocalUserDirectoryProvisioner(QString usersDirectory);

    [[nodiscard]] shared::common::Result<void> createInitialDirectories(
        const domain::account::AccountId& accountId) override;
    [[nodiscard]] shared::common::Result<void> removeUserDirectory(
        const domain::account::AccountId& accountId) override;

private:
    [[nodiscard]] QString accountPath(const domain::account::AccountId& accountId) const;
    QString m_usersDirectory;
};

} // namespace devhub::infrastructure::filesystem
