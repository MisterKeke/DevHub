#pragma once

#include "GitHubTypes.h"
#include "../../application/git/GitTypes.h"

namespace devhub::application::github {

class GitHubRepositoryResolver final
{
public:
    [[nodiscard]] static QVector<GitHubRepository> fromRemotes(const QVector<git::GitRemote>& remotes);
    [[nodiscard]] static std::optional<GitHubRepository> parseRemoteUrl(const QString& remoteName,
                                                                        const QString& url);
};

} // namespace devhub::application::github
