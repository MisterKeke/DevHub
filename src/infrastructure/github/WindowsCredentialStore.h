#pragma once

#include "../../application/github/IGitHubCredentialStore.h"

namespace devhub::infrastructure::github {

class WindowsCredentialStore final : public application::github::IGitHubCredentialStore
{
public:
    [[nodiscard]] bool load(const QString& localAccountId, application::github::GitHubCredentials& credentials,
                            QString& userSafeError) override;
    [[nodiscard]] bool save(const QString& localAccountId, const application::github::GitHubCredentials& credentials,
                            QString& userSafeError) override;
    [[nodiscard]] bool remove(const QString& localAccountId, QString& userSafeError) override;

private:
    [[nodiscard]] static QString targetName(const QString& localAccountId);
};

} // namespace devhub::infrastructure::github
