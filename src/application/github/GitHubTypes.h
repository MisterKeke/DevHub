#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace devhub::application::github {

struct GitHubCredentials final
{
    QString accessToken;
    QString clientId;
    QDateTime accessTokenExpiresAtUtc;
    QString refreshToken;
    QDateTime refreshTokenExpiresAtUtc;
    QString login;
    QString avatarUrl;
    QDateTime lastVerifiedAtUtc;
};

struct GitHubConnection final
{
    enum class State { NotConfigured, Disconnected, Connecting, WaitingForAuthorization,
                       Verifying, Connected, ReauthorizationRequired, Locked, Error };
    State state = State::Disconnected;
    QString login;
    QString avatarUrl;
    QString message;
    QDateTime lastVerifiedAtUtc;
};

struct GitHubRepository final
{
    QString remoteName;
    QString owner;
    QString name;
    [[nodiscard]] QString fullName() const { return owner + QLatin1Char('/') + name; }
    friend bool operator==(const GitHubRepository&, const GitHubRepository&) = default;
};

struct GitHubPullRequest final
{
    int number = 0;
    QString title;
    QString author;
    QString headBranch;
    QString baseBranch;
    QString state;
    bool draft = false;
    bool merged = false;
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
    QString htmlUrl;
    QString body;
    QString headSha;
    QString baseSha;
    QString mergeableState;
    int additions = 0;
    int deletions = 0;
    int changedFiles = 0;
};

struct GitHubPullRequestFile final
{
    QString path;
    QString status;
    int additions = 0;
    int deletions = 0;
    QString patch;
    bool patchUnavailable = false;
};

struct GitHubReview final
{
    QString reviewer;
    QString state;
    QString body;
    QDateTime submittedAtUtc;
};

struct GitHubDiscussionComment final
{
    QString author;
    QString body;
    QDateTime createdAtUtc;
};

struct GitHubCheckRun final
{
    QString name;
    QString status;
    QString conclusion;
    QString headSha;
    QString htmlUrl;
    QDateTime startedAtUtc;
    QDateTime completedAtUtc;
};

struct GitHubWorkflowRun final
{
    qint64 id = 0;
    QString name;
    QString status;
    QString conclusion;
    QString headSha;
    QString htmlUrl;
    QDateTime startedAtUtc;
    QDateTime completedAtUtc;
};

struct GitHubWorkflowJob final
{
    qint64 id = 0;
    QString name;
    QString status;
    QString conclusion;
    QString htmlUrl;
    QDateTime startedAtUtc;
    QDateTime completedAtUtc;
    QStringList failedSteps;
};

struct GitHubPullRequestDetails final
{
    GitHubPullRequest pullRequest;
    QVector<GitHubPullRequestFile> files;
    QVector<GitHubReview> reviews;
    QVector<GitHubDiscussionComment> comments;
    QVector<GitHubCheckRun> checks;
    QVector<GitHubWorkflowRun> workflowRuns;
    QVector<GitHubWorkflowJob> failedJobs;
    QStringList partialErrors;
};

struct GitHubMonitorSnapshot final
{
    QVector<GitHubPullRequest> pullRequests;
    QVector<GitHubWorkflowRun> workflowRuns;
};

[[nodiscard]] QString pullRequestStatusText(const GitHubPullRequest& pullRequest);
[[nodiscard]] QString checkRunStatusText(const QString& status, const QString& conclusion);

} // namespace devhub::application::github

Q_DECLARE_METATYPE(devhub::application::github::GitHubConnection)
Q_DECLARE_METATYPE(devhub::application::github::GitHubRepository)
Q_DECLARE_METATYPE(QVector<devhub::application::github::GitHubRepository>)
Q_DECLARE_METATYPE(devhub::application::github::GitHubPullRequest)
Q_DECLARE_METATYPE(QVector<devhub::application::github::GitHubPullRequest>)
Q_DECLARE_METATYPE(devhub::application::github::GitHubPullRequestDetails)
Q_DECLARE_METATYPE(devhub::application::github::GitHubWorkflowJob)
Q_DECLARE_METATYPE(QVector<devhub::application::github::GitHubWorkflowJob>)
Q_DECLARE_METATYPE(devhub::application::github::GitHubMonitorSnapshot)
