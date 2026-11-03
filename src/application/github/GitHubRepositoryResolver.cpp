#include "GitHubRepositoryResolver.h"

#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace devhub::application::github {
namespace {
bool validSegment(const QString& segment)
{
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9_.-]{1,100}$"));
    return allowed.match(segment).hasMatch() && segment != QStringLiteral(".")
        && segment != QStringLiteral("..");
}

std::optional<GitHubRepository> fromPath(const QString& remoteName, QString path)
{
    while (path.startsWith(QLatin1Char('/'))) path.remove(0, 1);
    if (path.endsWith(QLatin1Char('/'))) path.chop(1);
    if (path.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) path.chop(4);
    const auto pieces = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (pieces.size() != 2) return std::nullopt;
    const QString owner = QUrl::fromPercentEncoding(pieces.at(0).toUtf8());
    const QString name = QUrl::fromPercentEncoding(pieces.at(1).toUtf8());
    if (!validSegment(owner) || !validSegment(name)) return std::nullopt;
    return GitHubRepository{remoteName, owner, name};
}
}

std::optional<GitHubRepository> GitHubRepositoryResolver::parseRemoteUrl(const QString& remoteName,
                                                                          const QString& url)
{
    const QString value = url.trimmed();
    if (value.isEmpty()) return std::nullopt;
    for (const auto character : value)
        if (character.category() == QChar::Other_Control) return std::nullopt;

    static const QRegularExpression scp(QStringLiteral("^(?:git@)?github\\.com:(.+)$"),
                                         QRegularExpression::CaseInsensitiveOption);
    const auto scpMatch = scp.match(value);
    if (scpMatch.hasMatch()) return fromPath(remoteName, scpMatch.captured(1));

    const QUrl parsed(value, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) != 0)
        return std::nullopt;
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QStringLiteral("https") && scheme != QStringLiteral("ssh")) return std::nullopt;
    if (!parsed.query().isEmpty() || !parsed.fragment().isEmpty() || !parsed.password().isEmpty()) return std::nullopt;
    const QString user = parsed.userName(QUrl::FullyDecoded);
    if ((scheme == QStringLiteral("https") && !user.isEmpty())
        || (scheme == QStringLiteral("ssh") && !user.isEmpty() && user != QStringLiteral("git"))) return std::nullopt;
    const int port = parsed.port(-1);
    if (port != -1 && ((scheme == QStringLiteral("https") && port != 443)
                       || (scheme == QStringLiteral("ssh") && port != 22))) return std::nullopt;
    return fromPath(remoteName, parsed.path());
}

QVector<GitHubRepository> GitHubRepositoryResolver::fromRemotes(const QVector<git::GitRemote>& remotes)
{
    QVector<GitHubRepository> repositories;
    for (const auto& remote : remotes) {
        for (const auto& url : {remote.fetchUrl, remote.pushUrl}) {
            const auto urls = url.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
            for (const auto& oneUrl : urls) {
                auto parsed = parseRemoteUrl(remote.name, oneUrl);
                if (parsed && std::none_of(repositories.cbegin(), repositories.cend(), [&parsed](const auto& existing) {
                        return existing.remoteName == parsed->remoteName
                            && existing.owner.compare(parsed->owner, Qt::CaseInsensitive) == 0
                            && existing.name.compare(parsed->name, Qt::CaseInsensitive) == 0;
                    })) repositories.push_back(std::move(*parsed));
            }
        }
    }
    return repositories;
}

} // namespace devhub::application::github
