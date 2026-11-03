#include "../src/application/github/GitHubRepositoryResolver.h"
#include "../src/application/github/GitHubMonitorService.h"
#include "../src/application/github/GitHubService.h"
#include "../src/application/events/ApplicationEventHub.h"
#include "../src/application/notifications/NotificationService.h"
#include "../src/application/settings/SettingsService.h"
#include "../src/shared/identifiers/IdGenerator.h"
#include "../src/shared/time/ApplicationClock.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>

using namespace devhub::application::github;

namespace {
struct RecordedRequest final
{
    QByteArray method;
    QUrl url;
    QByteArray body;
    QByteArray authorization;
    qint64 atMs = 0;
};

class FakeReply final : public QNetworkReply
{
public:
    FakeReply(QNetworkAccessManager* manager, const QNetworkRequest& request, QNetworkAccessManager::Operation operation,
              QByteArray body, int status, QByteArray link = {}, QByteArray rateRemaining = {}, QByteArray rateReset = {})
        : QNetworkReply(manager), m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!link.isEmpty()) setRawHeader("Link", std::move(link));
        if (!rateRemaining.isEmpty()) setRawHeader("X-RateLimit-Remaining", std::move(rateRemaining));
        if (!rateReset.isEmpty()) setRawHeader("X-RateLimit-Reset", std::move(rateReset));
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }

    void abort() override
    {
        if (isFinished()) return;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char* destination, qint64 maximum) override
    {
        if (m_offset >= m_body.size()) return -1;
        const qint64 count = qMin(maximum, static_cast<qint64>(m_body.size()) - m_offset);
        std::memcpy(destination, m_body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class FakeGitHubNetwork final : public QNetworkAccessManager
{
public:
    enum class PollMode { PendingThenSuccess, SlowDown, Denied };
    PollMode pollMode = PollMode::PendingThenSuccess;
    QVector<RecordedRequest> requests;
    QVector<qint64> pollTimes;
    std::function<void()> onPoll;
    int pollCount = 0;
    bool rejectRefresh = false;
    bool rateLimitPulls = false;
    bool monitorPrMerged = false;
    bool monitorRunInProgress = false;
    bool rejectUserToken = false;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoingData = nullptr) override
    {
        RecordedRequest recorded;
        recorded.method = operation == PostOperation ? QByteArrayLiteral("POST")
            : operation == GetOperation ? QByteArrayLiteral("GET") : QByteArrayLiteral("OTHER");
        recorded.url = request.url();
        recorded.body = outgoingData ? outgoingData->readAll() : QByteArray{};
        recorded.authorization = request.rawHeader("Authorization");
        recorded.atMs = QDateTime::currentMSecsSinceEpoch();
        requests.push_back(recorded);

        const QString path = request.url().path();
        QByteArray response;
        int status = 200;
        QByteArray link;
        QByteArray rateRemaining;
        QByteArray rateReset;
        if (path == QStringLiteral("/login/device/code")) {
            response = R"({"device_code":"device-secret-fixture","user_code":"ABCD-EFGH","verification_uri":"https://github.com/login/device","expires_in":90,"interval":1})";
        } else if (path == QStringLiteral("/login/oauth/access_token")) {
            const QUrlQuery form(QString::fromUtf8(recorded.body));
            const QString grant = form.queryItemValue(QStringLiteral("grant_type"));
            if (grant == QStringLiteral("refresh_token")) {
                response = rejectRefresh
                    ? R"({"error":"bad_refresh_token"})"
                    : R"({"access_token":"replacement-access","expires_in":28800,"refresh_token":"replacement-refresh","refresh_token_expires_in":15897600,"token_type":"bearer"})";
            } else {
                ++pollCount;
                pollTimes.push_back(recorded.atMs);
                if (onPoll) onPoll();
                if (pollMode == PollMode::Denied) response = R"({"error":"access_denied"})";
                else if (pollMode == PollMode::SlowDown && pollCount == 1)
                    response = R"({"error":"slow_down","interval":6})";
                else if (pollMode == PollMode::PendingThenSuccess && pollCount == 1)
                    response = R"({"error":"authorization_pending"})";
                else response = R"({"access_token":"access-fixture","expires_in":28800,"refresh_token":"refresh-fixture","refresh_token_expires_in":15897600,"token_type":"bearer"})";
            }
        } else if (path == QStringLiteral("/user")) {
            if (request.rawHeader("Authorization").isEmpty()) status = 401;
            else if (rejectUserToken) {
                status = 401;
                response = R"({"message":"Bad credentials"})";
            }
            else response = R"({"login":"octocat","avatar_url":"https://avatars.githubusercontent.com/u/583231?v=4","html_url":"https://github.com/octocat"})";
        } else if (path == QStringLiteral("/repos/octocat/Hello-World/pulls")) {
            if (rateLimitPulls) {
                status = 403;
                response = R"({"message":"API rate limit exceeded"})";
                rateRemaining = QByteArrayLiteral("0");
                rateReset = QByteArray::number(QDateTime::currentSecsSinceEpoch() + 60);
            } else if (monitorPrMerged) {
                response = R"([{"number":42,"title":"Fixture PR","user":{"login":"octocat"},"head":{"ref":"feature","sha":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},"base":{"ref":"main","sha":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},"state":"closed","draft":false,"merged_at":"2026-11-03T10:00:00Z","created_at":"2026-11-01T10:00:00Z","updated_at":"2026-11-03T10:00:00Z","html_url":"https://github.com/octocat/Hello-World/pull/42"}])";
            } else {
                response = R"([{"number":42,"title":"Fixture PR","user":{"login":"octocat"},"head":{"ref":"feature","sha":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},"base":{"ref":"main","sha":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},"state":"open","draft":true,"merged_at":null,"created_at":"2026-11-01T10:00:00Z","updated_at":"2026-11-02T10:00:00Z","html_url":"https://github.com/octocat/Hello-World/pull/42"}])";
                link = R"(<https://api.github.com/repos/octocat/Hello-World/pulls?page=2>; rel="next")";
            }
        } else if (path == QStringLiteral("/repos/octocat/Hello-World/actions/runs")) {
            response = monitorRunInProgress
                ? R"({"total_count":1,"workflow_runs":[{"id":7,"name":"CI","status":"in_progress","conclusion":null,"head_sha":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","html_url":"https://github.com/octocat/Hello-World/actions/runs/7","run_started_at":"2026-11-02T09:00:00Z","created_at":"2026-11-02T09:00:00Z","updated_at":"2026-11-02T09:03:00Z"}]})"
                : R"({"total_count":1,"workflow_runs":[{"id":7,"name":"CI","status":"completed","conclusion":"success","head_sha":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","html_url":"https://github.com/octocat/Hello-World/actions/runs/7","run_started_at":"2026-11-02T09:00:00Z","created_at":"2026-11-02T09:00:00Z","updated_at":"2026-11-02T09:05:00Z"}]})";
        } else {
            status = 404;
            response = R"({"message":"Not Found"})";
        }
        return new FakeReply(this, request, operation, response, status, link, rateRemaining, rateReset);
    }
};

class MemorySettingsRepository final : public devhub::application::settings::ISettingsRepository
{
public:
    devhub::application::settings::SettingValues global;
    QHash<QString, devhub::application::settings::SettingValues> workspaces;

    devhub::shared::common::Result<devhub::application::settings::SettingValues> load(
        devhub::application::settings::SettingScope scope, const QString& workspaceId = {}) override
    {
        return devhub::shared::common::Result<devhub::application::settings::SettingValues>::success(
            scope == devhub::application::settings::SettingScope::Workspace
                ? workspaces.value(workspaceId) : global);
    }

    devhub::shared::common::Result<void> save(devhub::application::settings::SettingScope scope,
        const QString& workspaceId, const QString& id, const QVariant& value) override
    {
        if (scope == devhub::application::settings::SettingScope::Workspace)
            workspaces[workspaceId].insert(id, value);
        else global.insert(id, value);
        return devhub::shared::common::Result<void>::success();
    }

    devhub::shared::common::Result<void> saveBatch(
        const devhub::application::settings::SettingUpdates& updates) override
    {
        for (const auto& update : updates) {
            const auto result = save(update.scope, update.workspaceId, update.id, update.value);
            if (!result) return result;
        }
        return devhub::shared::common::Result<void>::success();
    }
};

class TestClock final : public devhub::shared::time::IApplicationClock
{
public:
    QDateTime utcNow() const override { return QDateTime::currentDateTimeUtc(); }
};

class MemoryCredentialStore final : public IGitHubCredentialStore
{
public:
    GitHubCredentials value;
    bool present = false;
    bool failSave = false;
    bool removed = false;

    bool load(const QString&, GitHubCredentials& credentials, QString& error) override
    {
        Q_UNUSED(error);
        if (present) credentials = value;
        return true;
    }
    bool save(const QString&, const GitHubCredentials& credentials, QString& error) override
    {
        if (failSave) {
            error = QStringLiteral("secure store fixture failure");
            return false;
        }
        value = credentials;
        present = true;
        return true;
    }
    bool remove(const QString&, QString& error) override
    {
        Q_UNUSED(error);
        present = false;
        removed = true;
        value = {};
        return true;
    }
};

std::unique_ptr<MemoryCredentialStore> makeStore(MemoryCredentialStore** storeOut)
{
    auto store = std::make_unique<MemoryCredentialStore>();
    *storeOut = store.get();
    return store;
}
}

class GitHubTests final : public QObject
{
    Q_OBJECT
private slots:
    void parsesSupportedRemoteForms();
    void rejectsUntrustedOrAmbiguousRemoteUrls();
    void mapsCheckStateAndConclusionSeparately();
    void runsDeviceFlowAsynchronouslyAndStoresOnlyAfterProfileVerification();
    void reportsDeviceApprovalFollowedByApiTokenRejection();
    void cancellationIgnoresTheLatePollResponse();
    void slowDownAddsTheDocumentedDelay();
    void restoresAndRotatesRefreshCredentials();
    void lockUnlockAndDisconnectRespectCredentialBoundary();
    void revokedRefreshTokenRequiresReauthorization();
    void changingClientIdRequiresReauthorization();
    void secureStoreFailureDoesNotClaimConnection();
    void missingClientIdDoesNotStartNetworkRequests();
    void acceptsLegacyAndCurrentGitHubAppClientIdFormats();
    void loadsPullRequestsWithPaginationAndASeparateStatus();
    void monitorSnapshotUsesBoundedAuthorizedEndpoints();
    void cancellationInvalidatesMonitorResponses();
    void authorizationDenialDoesNotPersistCredentials();
    void expiredAccessWithoutRefreshRequiresAuthorization();
    void reportsGitHubRateLimitReset();
    void workspaceMonitoringNotifiesOnTransitionsAndRestoresItsBaseline();
};

void GitHubTests::parsesSupportedRemoteForms()
{
    const auto https = GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("https://github.com/octocat/Hello-World.git"));
    QVERIFY(https.has_value());
    QCOMPARE(https->owner, QStringLiteral("octocat"));
    QCOMPARE(https->name, QStringLiteral("Hello-World"));
    QVERIFY(GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("upstream"),
        QStringLiteral("git@github.com:octocat/Hello-World.git")).has_value());
    QVERIFY(GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("ssh"),
        QStringLiteral("ssh://git@github.com/octocat/Hello-World.git")).has_value());

    QVector<devhub::application::git::GitRemote> remotes{
        {QStringLiteral("origin"), QStringLiteral("https://github.com/octocat/Hello-World.git"),
         QStringLiteral("git@github.com:octocat/Hello-World.git")},
        {QStringLiteral("upstream"), QStringLiteral("https://github.com/acme/project"), {}}
    };
    const auto repositories = GitHubRepositoryResolver::fromRemotes(remotes);
    QCOMPARE(repositories.size(), 2);
    QCOMPARE(repositories.at(0).remoteName, QStringLiteral("origin"));
    QCOMPARE(repositories.at(1).fullName(), QStringLiteral("acme/project"));
}

void GitHubTests::rejectsUntrustedOrAmbiguousRemoteUrls()
{
    QVERIFY(!GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("https://github.com.evil.test/octocat/repo")).has_value());
    QVERIFY(!GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("http://github.com/octocat/repo")).has_value());
    QVERIFY(!GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("https://github.com/octocat/repo/tree/main")).has_value());
    QVERIFY(!GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("ssh://git@gitlab.com/octocat/repo.git")).has_value());
    QVERIFY(!GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("https://token@github.com/octocat/repo.git")).has_value());
    QVERIFY(!GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("https://github.com:22/octocat/repo.git")).has_value());
    QVERIFY(!GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("ssh://alice@github.com/octocat/repo.git")).has_value());
    QVERIFY(!GitHubRepositoryResolver::parseRemoteUrl(QStringLiteral("origin"),
        QStringLiteral("git@github.com:../repo.git")).has_value());
}

void GitHubTests::mapsCheckStateAndConclusionSeparately()
{
    QCOMPARE(checkRunStatusText(QStringLiteral("queued"), {}), QStringLiteral("Queued"));
    QCOMPARE(checkRunStatusText(QStringLiteral("in_progress"), {}), QStringLiteral("In progress"));
    QCOMPARE(checkRunStatusText(QStringLiteral("completed"), QStringLiteral("success")), QStringLiteral("Success"));
    QCOMPARE(checkRunStatusText(QStringLiteral("completed"), QStringLiteral("failure")), QStringLiteral("Failure"));
    QCOMPARE(checkRunStatusText(QStringLiteral("completed"), QStringLiteral("cancelled")), QStringLiteral("Cancelled"));
    QCOMPARE(checkRunStatusText(QStringLiteral("completed"), QStringLiteral("skipped")), QStringLiteral("Skipped"));
    QCOMPARE(checkRunStatusText(QStringLiteral("completed"), QStringLiteral("neutral")), QStringLiteral("Neutral"));
    QCOMPARE(checkRunStatusText(QStringLiteral("completed"), {}), QStringLiteral("Unknown"));
}

void GitHubTests::runsDeviceFlowAsynchronouslyAndStoresOnlyAfterProfileVerification()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        makeStore(&stored), nullptr, &network);
    QSignalSpy codeSpy(&service, &GitHubService::deviceCodeReady);
    QSignalSpy doneSpy(&service, &GitHubService::deviceFlowFinished);
    service.beginDeviceFlow();
    QTRY_COMPARE_WITH_TIMEOUT(codeSpy.count(), 1, 4000);
    QCOMPARE(codeSpy.at(0).at(0).toString(), QStringLiteral("ABCD-EFGH"));
    QCOMPARE(codeSpy.at(0).at(1).toUrl().toString(), QStringLiteral("https://github.com/login/device"));
    QVERIFY(!stored->present);
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 7000);
    QVERIFY(doneSpy.at(0).at(0).toBool());
    QVERIFY(stored->present);
    QCOMPARE(stored->value.accessToken, QStringLiteral("access-fixture"));
    QCOMPARE(stored->value.refreshToken, QStringLiteral("refresh-fixture"));
    QCOMPARE(stored->value.clientId, QStringLiteral("Iv1.testClientId"));
    QCOMPARE(service.connection().login, QStringLiteral("octocat"));
    QVERIFY(service.connection().message.contains(QStringLiteral("octocat")));
    QVERIFY(!service.connection().message.contains(QStringLiteral("access-fixture")));
    const auto profileRequest = std::find_if(network.requests.cbegin(), network.requests.cend(), [](const auto& request) {
        return request.url.path() == QStringLiteral("/user");
    });
    QVERIFY(profileRequest != network.requests.cend());
    QCOMPARE(profileRequest->authorization, QByteArrayLiteral("Bearer access-fixture"));
    QCOMPARE(network.pollTimes.size(), 2);
    QVERIFY(network.pollTimes.at(1) - network.pollTimes.at(0) >= 950);

    const auto codeRequest = network.requests.first();
    QCOMPARE(codeRequest.url.path(), QStringLiteral("/login/device/code"));
    QVERIFY(codeRequest.url.query().isEmpty());
    QVERIFY(QUrlQuery(QString::fromUtf8(codeRequest.body)).hasQueryItem(QStringLiteral("client_id")));
    const auto poll = std::find_if(network.requests.cbegin(), network.requests.cend(), [](const auto& request) {
        return request.url.path() == QStringLiteral("/login/oauth/access_token");
    });
    QVERIFY(poll != network.requests.cend());
    QVERIFY(QUrlQuery(QString::fromUtf8(poll->body)).hasQueryItem(QStringLiteral("device_code")));
    QCOMPARE(poll->authorization, QByteArray{});
}

void GitHubTests::reportsDeviceApprovalFollowedByApiTokenRejection()
{
    FakeGitHubNetwork network;
    network.rejectUserToken = true;
    MemoryCredentialStore* stored = nullptr;
    GitHubService service(QStringLiteral("Iv23liExample123"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        makeStore(&stored), nullptr, &network);
    QSignalSpy done(&service, &GitHubService::deviceFlowFinished);
    service.beginDeviceFlow();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 7000);
    QVERIFY(!done.first().at(0).toBool());
    const QString message = done.first().at(1).toString();
    QVERIFY(message.contains(QStringLiteral("GET /user")));
    QVERIFY(message.contains(QStringLiteral("HTTP 401")));
    QVERIFY(message.contains(QStringLiteral("Bad credentials")));
    QVERIFY(!message.contains(QStringLiteral("access-fixture")));
    QVERIFY(!stored->present);
}

void GitHubTests::cancellationIgnoresTheLatePollResponse()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        makeStore(&stored), nullptr, &network);
    network.onPoll = [&service] { service.cancelDeviceFlow(); };
    QSignalSpy doneSpy(&service, &GitHubService::deviceFlowFinished);
    service.beginDeviceFlow();
    QTRY_VERIFY_WITH_TIMEOUT(network.pollCount >= 1, 5000);
    QTest::qWait(200);
    QCOMPARE(doneSpy.count(), 0);
    QVERIFY(!stored->present);
    QVERIFY(service.connection().state == GitHubConnection::State::Disconnected
        || service.connection().state == GitHubConnection::State::Error);
}

void GitHubTests::slowDownAddsTheDocumentedDelay()
{
    FakeGitHubNetwork network;
    network.pollMode = FakeGitHubNetwork::PollMode::SlowDown;
    MemoryCredentialStore* stored = nullptr;
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        makeStore(&stored), nullptr, &network);
    service.beginDeviceFlow();
    QTRY_COMPARE_WITH_TIMEOUT(network.pollCount, 1, 5000);
    QTest::qWait(2500);
    QCOMPARE(network.pollCount, 1);
    service.cancelDeviceFlow();
    QVERIFY(!stored->present);
}

void GitHubTests::restoresAndRotatesRefreshCredentials()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("expired-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(-5);
    stored->value.refreshToken = QStringLiteral("valid-refresh");
    stored->value.refreshTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addDays(30);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);
    service.recheckConnection();
    QTRY_COMPARE_WITH_TIMEOUT(service.connection().state, GitHubConnection::State::Connected, 4000);
    QCOMPARE(stored->value.accessToken, QStringLiteral("replacement-access"));
    QCOMPARE(stored->value.refreshToken, QStringLiteral("replacement-refresh"));
    QCOMPARE(stored->value.login, QStringLiteral("octocat"));
    QVERIFY(std::any_of(network.requests.cbegin(), network.requests.cend(), [](const auto& request) {
        return request.url.path() == QStringLiteral("/login/oauth/access_token")
            && QUrlQuery(QString::fromUtf8(request.body)).queryItemValue(QStringLiteral("grant_type"))
                == QStringLiteral("refresh_token");
    }));
}

void GitHubTests::lockUnlockAndDisconnectRespectCredentialBoundary()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("active-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(3600);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);

    service.setSessionActive(false);
    QCOMPARE(service.connection().state, GitHubConnection::State::Locked);
    const qsizetype requestCountWhileLocked = network.requests.size();
    service.loadPullRequests({QStringLiteral("origin"), QStringLiteral("octocat"), QStringLiteral("Hello-World")},
        QStringLiteral("open"), 1);
    QTest::qWait(100);
    QCOMPARE(network.requests.size(), requestCountWhileLocked);

    service.setSessionActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(service.connection().state, GitHubConnection::State::Connected, 3000);
    QVERIFY(stored->present);
    service.disconnectGitHub();
    QVERIFY(stored->removed);
    QVERIFY(!stored->present);
    QCOMPARE(service.connection().state, GitHubConnection::State::Disconnected);
}

void GitHubTests::revokedRefreshTokenRequiresReauthorization()
{
    FakeGitHubNetwork network;
    network.rejectRefresh = true;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("expired-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(-5);
    stored->value.refreshToken = QStringLiteral("revoked-refresh");
    stored->value.refreshTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addDays(10);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);

    service.recheckConnection();
    QTRY_COMPARE_WITH_TIMEOUT(service.connection().state, GitHubConnection::State::ReauthorizationRequired, 3000);
    QVERIFY(service.connection().message.contains(QStringLiteral("Reconnect")));
    const qsizetype requestsAfterFailure = network.requests.size();
    service.loadPullRequests({QStringLiteral("origin"), QStringLiteral("octocat"), QStringLiteral("Hello-World")},
        QStringLiteral("open"), 1);
    QTest::qWait(100);
    QCOMPARE(network.requests.size(), requestsAfterFailure);
    QCOMPARE(stored->value.refreshToken, QStringLiteral("revoked-refresh"));
}

void GitHubTests::changingClientIdRequiresReauthorization()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("active-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(3600);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);
    service.recheckConnection();
    QTRY_COMPARE_WITH_TIMEOUT(service.connection().state, GitHubConnection::State::Connected, 3000);

    const qsizetype beforeConfigurationChange = network.requests.size();
    service.setClientId(QStringLiteral("Iv1.replacementClientId"));
    QCOMPARE(service.connection().state, GitHubConnection::State::ReauthorizationRequired);
    QCOMPARE(network.requests.size(), beforeConfigurationChange);
}

void GitHubTests::secureStoreFailureDoesNotClaimConnection()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->failSave = true;
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);
    QSignalSpy doneSpy(&service, &GitHubService::deviceFlowFinished);
    service.beginDeviceFlow();
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 7000);
    QVERIFY(!doneSpy.at(0).at(0).toBool());
    QVERIFY(!stored->present);
    QVERIFY(service.connection().state != GitHubConnection::State::Connected);
    QVERIFY(!service.connection().message.contains(QStringLiteral("access-fixture")));
}

void GitHubTests::missingClientIdDoesNotStartNetworkRequests()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    GitHubService service({}, QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        makeStore(&stored), nullptr, &network);
    QSignalSpy doneSpy(&service, &GitHubService::deviceFlowFinished);
    service.beginDeviceFlow();
    QCOMPARE(network.requests.size(), 0);
    QCOMPARE(doneSpy.count(), 1);
    QVERIFY(!doneSpy.at(0).at(0).toBool());
    QCOMPARE(service.connection().state, GitHubConnection::State::NotConfigured);
}

void GitHubTests::acceptsLegacyAndCurrentGitHubAppClientIdFormats()
{
    FakeGitHubNetwork legacyNetwork;
    MemoryCredentialStore* legacyStore = nullptr;
    GitHubService legacy(QStringLiteral("Iv1.abc123"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        makeStore(&legacyStore), nullptr, &legacyNetwork);
    QVERIFY(legacy.hasValidClientId());

    FakeGitHubNetwork currentNetwork;
    MemoryCredentialStore* currentStore = nullptr;
    GitHubService current(QStringLiteral("Iv23liExample123"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        makeStore(&currentStore), nullptr, &currentNetwork);
    QVERIFY(current.hasValidClientId());
    QCOMPARE(current.connection().state, GitHubConnection::State::Disconnected);

    current.setClientId(QStringLiteral("12345678"));
    QVERIFY(!current.hasValidClientId());
    QCOMPARE(current.connection().state, GitHubConnection::State::NotConfigured);
}

void GitHubTests::loadsPullRequestsWithPaginationAndASeparateStatus()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("active-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(3600);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);
    service.recheckConnection();
    QTRY_COMPARE_WITH_TIMEOUT(service.connection().state, GitHubConnection::State::Connected, 3000);
    QSignalSpy loaded(&service, &GitHubService::pullRequestsLoaded);
    service.loadPullRequests({QStringLiteral("origin"), QStringLiteral("octocat"), QStringLiteral("Hello-World")},
        QStringLiteral("open"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
    const auto requests = loaded.takeFirst();
    const auto pullRequests = qvariant_cast<QVector<GitHubPullRequest>>(requests.at(0));
    QCOMPARE(pullRequests.size(), 1);
    QCOMPARE(pullRequests.first().number, 42);
    QVERIFY(pullRequests.first().draft);
    QVERIFY(!pullRequests.first().merged);
    QVERIFY(requests.at(1).toBool());
    QCOMPARE(requests.at(2).toInt(), 1);
    QVERIFY(std::any_of(network.requests.cbegin(), network.requests.cend(), [](const auto& request) {
        return request.url.path() == QStringLiteral("/repos/octocat/Hello-World/pulls")
            && request.authorization == QByteArrayLiteral("Bearer active-access");
    }));
}

void GitHubTests::monitorSnapshotUsesBoundedAuthorizedEndpoints()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("active-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(3600);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);
    service.recheckConnection();
    QTRY_COMPARE_WITH_TIMEOUT(service.connection().state, GitHubConnection::State::Connected, 3000);
    QSignalSpy loaded(&service, &GitHubService::monitorSnapshotLoaded);
    service.loadMonitorSnapshot({QStringLiteral("origin"), QStringLiteral("octocat"), QStringLiteral("Hello-World")});
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
    const auto snapshot = qvariant_cast<GitHubMonitorSnapshot>(loaded.at(0).at(0));
    QCOMPARE(snapshot.pullRequests.size(), 1);
    QCOMPARE(snapshot.workflowRuns.size(), 1);
    QCOMPARE(snapshot.workflowRuns.first().conclusion, QStringLiteral("success"));
    int authorizedRequests = 0;
    for (const auto& request : network.requests) {
        if (request.url.path().startsWith(QStringLiteral("/repos/octocat/Hello-World/"))) {
            QCOMPARE(request.authorization, QByteArrayLiteral("Bearer active-access"));
            ++authorizedRequests;
        }
    }
    QCOMPARE(authorizedRequests, 2);
}

void GitHubTests::cancellationInvalidatesMonitorResponses()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("active-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(3600);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);
    service.recheckConnection();
    QTRY_COMPARE_WITH_TIMEOUT(service.connection().state, GitHubConnection::State::Connected, 3000);
    QSignalSpy loaded(&service, &GitHubService::monitorSnapshotLoaded);
    service.loadMonitorSnapshot({QStringLiteral("origin"), QStringLiteral("octocat"), QStringLiteral("Hello-World")});
    service.cancelMonitoringRequests();
    QTest::qWait(100);
    QCOMPARE(loaded.count(), 0);
}

void GitHubTests::authorizationDenialDoesNotPersistCredentials()
{
    FakeGitHubNetwork network;
    network.pollMode = FakeGitHubNetwork::PollMode::Denied;
    MemoryCredentialStore* stored = nullptr;
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        makeStore(&stored), nullptr, &network);
    QSignalSpy done(&service, &GitHubService::deviceFlowFinished);
    service.beginDeviceFlow();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QVERIFY(!done.first().at(0).toBool());
    QVERIFY(!stored->present);
    QCOMPARE(service.connection().state, GitHubConnection::State::Error);
    QVERIFY(service.connection().message.contains(QStringLiteral("denied"), Qt::CaseInsensitive));
}

void GitHubTests::expiredAccessWithoutRefreshRequiresAuthorization()
{
    FakeGitHubNetwork network;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("expired-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(-1);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);
    service.recheckConnection();
    QCOMPARE(service.connection().state, GitHubConnection::State::ReauthorizationRequired);
    QVERIFY(service.connection().message.contains(QStringLiteral("Reconnect")));
    QVERIFY(std::none_of(network.requests.cbegin(), network.requests.cend(), [](const auto& request) {
        return request.url.path() == QStringLiteral("/login/oauth/access_token");
    }));
}

void GitHubTests::reportsGitHubRateLimitReset()
{
    FakeGitHubNetwork network;
    network.rateLimitPulls = true;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("active-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(3600);
    stored->value.login = QStringLiteral("old-login");
    GitHubService service(QStringLiteral("Iv1.testClientId"), QStringLiteral("12345678-1234-1234-1234-123456789abc"),
        std::move(store), nullptr, &network);
    service.recheckConnection();
    QTRY_COMPARE_WITH_TIMEOUT(service.connection().state, GitHubConnection::State::Connected, 3000);
    QSignalSpy loaded(&service, &GitHubService::pullRequestsLoaded);
    service.loadPullRequests({QStringLiteral("origin"), QStringLiteral("octocat"), QStringLiteral("Hello-World")},
        QStringLiteral("open"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
    QVERIFY(loaded.first().at(3).toString().contains(QStringLiteral("rate limit"), Qt::CaseInsensitive));
    QVERIFY(loaded.first().at(3).toString().contains(QStringLiteral("Try again after")));
}

void GitHubTests::workspaceMonitoringNotifiesOnTransitionsAndRestoresItsBaseline()
{
    constexpr auto workspaceId = "12345678-1234-1234-1234-123456789abc";
    FakeGitHubNetwork network;
    network.monitorRunInProgress = true;
    MemoryCredentialStore* stored = nullptr;
    auto store = makeStore(&stored);
    stored->present = true;
    stored->value.clientId = QStringLiteral("Iv1.testClientId");
    stored->value.accessToken = QStringLiteral("active-access");
    stored->value.accessTokenExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(3600);
    stored->value.login = QStringLiteral("old-login");
    GitHubService github(QStringLiteral("Iv1.testClientId"), QString::fromLatin1(workspaceId),
        std::move(store), nullptr, &network);
    github.recheckConnection();
    QTRY_COMPARE_WITH_TIMEOUT(github.connection().state, GitHubConnection::State::Connected, 3000);

    MemorySettingsRepository settingsRepository;
    devhub::application::settings::SettingsService settings(settingsRepository);
    QVERIFY(settings.setValue(QStringLiteral("github/monitoringEnabled"), true, QString::fromLatin1(workspaceId)));
    QVERIFY(settings.setValue(QStringLiteral("github/refreshIntervalSeconds"), 3600, QString::fromLatin1(workspaceId)));
    TestClock clock;
    devhub::shared::identifiers::UuidIdGenerator ids;
    devhub::application::events::ApplicationEventHub events;
    devhub::application::notifications::NotificationService notifications(clock, ids, events);
    QSignalSpy raised(&events, &devhub::application::events::ApplicationEventHub::notificationRaised);
    devhub::application::github::GitHubMonitorService monitor(github, settings, notifications);
    const devhub::application::github::GitHubRepository repository{
        QStringLiteral("origin"), QStringLiteral("octocat"), QStringLiteral("Hello-World")};
    monitor.setTarget(QString::fromLatin1(workspaceId), repository);
    QTRY_VERIFY_WITH_TIMEOUT(!settings.value(QStringLiteral("github/monitorSnapshot"), QString::fromLatin1(workspaceId))
        .toString().isEmpty(), 5000);
    QCOMPARE(raised.count(), 0); // A first poll establishes a baseline without backfilling notifications.

    network.monitorPrMerged = true;
    network.monitorRunInProgress = false;
    monitor.configurationChanged();
    QTRY_COMPARE_WITH_TIMEOUT(raised.count(), 2, 5000);
    bool sawMerge = false;
    bool sawActions = false;
    for (const auto& item : raised) {
        const auto notification = qvariant_cast<devhub::application::notifications::Notification>(item.at(0));
        QCOMPARE(notification.category, devhub::application::notifications::NotificationCategory::GitHub);
        QCOMPARE(notification.workspaceId.value().toString(QUuid::WithoutBraces), QString::fromLatin1(workspaceId));
        sawMerge = sawMerge || notification.title.contains(QStringLiteral("merged"), Qt::CaseInsensitive);
        sawActions = sawActions || notification.title.contains(QStringLiteral("Actions"));
        QVERIFY(notification.deduplicationKey.startsWith(QStringLiteral("github:")));
    }
    QVERIFY(sawMerge);
    QVERIFY(sawActions);

    const qsizetype beforeRestart = network.requests.size();
    devhub::application::github::GitHubMonitorService resumed(github, settings, notifications);
    resumed.setTarget(QString::fromLatin1(workspaceId), repository);
    QTRY_VERIFY_WITH_TIMEOUT(network.requests.size() >= beforeRestart + 2, 5000);
    QTest::qWait(150);
    QCOMPARE(raised.count(), 2); // The persisted snapshot prevents replay after a new monitor starts.
}

QTEST_GUILESS_MAIN(GitHubTests)
#include "github_test.moc"
