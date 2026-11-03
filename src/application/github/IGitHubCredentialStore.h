#pragma once

#include "GitHubTypes.h"

namespace devhub::application::github {

class IGitHubCredentialStore
{
public:
    virtual ~IGitHubCredentialStore() = default;
    [[nodiscard]] virtual bool load(const QString& localAccountId, GitHubCredentials& credentials,
                                    QString& userSafeError) = 0;
    [[nodiscard]] virtual bool save(const QString& localAccountId, const GitHubCredentials& credentials,
                                    QString& userSafeError) = 0;
    [[nodiscard]] virtual bool remove(const QString& localAccountId, QString& userSafeError) = 0;
};

} // namespace devhub::application::github
