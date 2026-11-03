#pragma once

#include "GitHubTypes.h"
#include "IGitHubCredentialStore.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>
#include <optional>

namespace devhub::application::github {

class GitHubService final : public QObject
{
    Q_OBJECT
public:
    using CredentialStore = std::unique_ptr<IGitHubCredentialStore>;

    GitHubService(QString clientId, QString localAccountId, CredentialStore credentialStore,
                  QObject* parent = nullptr, QNetworkAccessManager* network = nullptr);
    ~GitHubService() override;

    [[nodiscard]] const GitHubConnection& connection() const noexcept { return m_connection; }
    [[nodiscard]] bool hasValidClientId() const;
    void setClientId(QString clientId);
    void setSessionActive(bool active);
    void beginDeviceFlow();
    void cancelDeviceFlow();
    void disconnectGitHub();
    void recheckConnection();
    void cancelDashboardRequests();
    void loadPullRequests(const GitHubRepository& repository, QString state, int page);
    void loadPullRequestDetails(const GitHubRepository& repository, int number);
    void loadWorkflowHistory(const GitHubRepository& repository, int page);
    void loadWorkflowRunJobs(const GitHubRepository& repository, qint64 runId);
    void loadWorkflowJobLogs(const GitHubRepository& repository, qint64 jobId);
    void loadMonitorSnapshot(const GitHubRepository& repository);
    void cancelMonitoringRequests();

signals:
    void connectionChanged(devhub::application::github::GitHubConnection connection);
    void deviceCodeReady(QString userCode, QUrl verificationUri, int expiresInSeconds);
    void authorizationTimeRemaining(int remainingSeconds);
    void deviceFlowFinished(bool succeeded, QString message);
    void pullRequestsLoaded(QVector<devhub::application::github::GitHubPullRequest> pullRequests,
                            bool hasMore, int page, QString error);
    void pullRequestDetailsLoaded(devhub::application::github::GitHubPullRequestDetails details,
                                  QString error);
    void workflowRunJobsLoaded(qint64 runId,
                               QVector<devhub::application::github::GitHubWorkflowJob> jobs,
                               QString error);
    void workflowHistoryLoaded(QVector<devhub::application::github::GitHubWorkflowRun> runs,
                               bool hasMore, int page, QString error);
    void workflowJobLogsLoaded(qint64 jobId, QString text, bool truncated, QString error);
    void monitorSnapshotLoaded(devhub::application::github::GitHubMonitorSnapshot snapshot, QString error);

private:
    using JsonCallback = std::function<void(QJsonDocument, QString, int, QByteArray)>;
    struct TokenResponse;
    void performRequest(QNetworkRequest request, QByteArray method, QByteArray payload,
                        JsonCallback completion, qsizetype responseLimit,
                        bool deviceFlowRequest, quint64 flowGeneration, bool authRequest = false,
                        bool monitorRequest = false);

    void setConnection(GitHubConnection::State state, QString message = {});
    void restoreCredentials();
    void pollDeviceCode(quint64 generation);
    void finishDeviceFlow(quint64 generation, bool succeeded, QString message);
    void exchangeDeviceToken(quint64 generation);
    void verifyUser(QString accessToken, std::function<void(bool, QString, QString, QString)> completion,
                    bool deviceFlowRequest = false, quint64 flowGeneration = 0);
    void ensureValidToken(std::function<void(bool, QString)> completion);
    void refreshAccessToken(std::function<void(bool, QString)> completion);
    void requestOAuth(QByteArray form, QString endpoint, JsonCallback completion, bool deviceFlowRequest = false,
                      quint64 flowGeneration = 0);
    void requestApi(QString path, JsonCallback completion, bool deviceFlowRequest = false,
                    quint64 flowGeneration = 0, bool monitorRequest = false);
    void requestApiWithToken(QString path, QString accessToken, JsonCallback completion,
                             bool deviceFlowRequest = false, quint64 flowGeneration = 0,
                             bool authRequest = false, bool monitorRequest = false);
    [[nodiscard]] bool persistCredentials(const GitHubCredentials& credentials, QString& error);
    [[nodiscard]] static std::optional<TokenResponse> parseTokenResponse(const QJsonDocument& document,
                                                                          QString& errorCode);
    [[nodiscard]] static bool trustedGitHubUrl(const QString& url);
    void updateConnectionFromCredentials();

    QString m_clientId;
    QString m_localAccountId;
    CredentialStore m_credentialStore;
    QNetworkAccessManager m_ownedNetwork;
    QNetworkAccessManager* m_network = nullptr;
    QTimer m_pollTimer;
    QTimer m_countdownTimer;
    QPointer<QNetworkReply> m_flowReply;
    QPointer<QNetworkReply> m_flowProfileReply;
    QPointer<QNetworkReply> m_authReply;
    QPointer<QNetworkReply> m_authApiReply;
    QVector<QPointer<QNetworkReply>> m_dashboardReplies;
    QVector<QPointer<QNetworkReply>> m_monitorReplies;
    GitHubConnection m_connection;
    GitHubCredentials m_credentials;
    QString m_deviceCode;
    QUrl m_verificationUri;
    QDateTime m_deviceExpiresAtUtc;
    int m_pollIntervalSeconds = 5;
    bool m_sessionActive = true;
    bool m_hasCredentials = false;
    bool m_refreshInFlight = false;
    QVector<std::function<void(bool, QString)>> m_refreshWaiters;
    quint64 m_flowGeneration = 0;
    quint64 m_dashboardGeneration = 0;
    quint64 m_monitorGeneration = 0;
};

} // namespace devhub::application::github
