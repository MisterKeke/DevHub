#include "GitHubService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QTimeZone>
#include <QUrlQuery>

#include <algorithm>
#include <limits>
#include <memory>

namespace devhub::application::github {
namespace {
constexpr qsizetype OAuthResponseLimit = 64 * 1024;
constexpr qsizetype ApiResponseLimit = 4 * 1024 * 1024;
constexpr int RequestTimeoutMs = 20000;
constexpr auto DeviceCodeEndpoint = "https://github.com/login/device/code";
constexpr auto TokenEndpoint = "https://github.com/login/oauth/access_token";
constexpr auto ApiRoot = "https://api.github.com";
constexpr auto ApiVersion = "2022-11-28";

QString timestamp(const QJsonValue& value)
{
    const QString raw = value.toString();
    auto parsed = QDateTime::fromString(raw, Qt::ISODateWithMs);
    if (!parsed.isValid()) parsed = QDateTime::fromString(raw, Qt::ISODate);
    return parsed.isValid() ? parsed.toUTC().toString(Qt::ISODateWithMs) : QString{};
}

QDateTime dateValue(const QJsonValue& value)
{
    return QDateTime::fromString(timestamp(value), Qt::ISODateWithMs).toUTC();
}

QString boundedText(const QJsonValue& value, qsizetype maximum)
{
    QString text = value.toString();
    if (text.size() > maximum) text.truncate(maximum);
    return text;
}

bool validRepoSegment(const QString& value)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9_.-]{1,100}$"));
    return expression.match(value).hasMatch() && value != QStringLiteral(".")
        && value != QStringLiteral("..");
}

QString repoPath(const GitHubRepository& repository)
{
    if (!validRepoSegment(repository.owner) || !validRepoSegment(repository.name)) return {};
    return QStringLiteral("/repos/%1/%2").arg(repository.owner, repository.name);
}

bool validGitHubWebUrl(const QString& value)
{
    const QUrl url(value, QUrl::StrictMode);
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0
        && url.userInfo().isEmpty() && (url.port(-1) == -1 || url.port() == 443);
}

QString safeApiErrorMessage(const QJsonDocument& document)
{
    if (!document.isObject()) return {};
    QString message = boundedText(document.object().value(QStringLiteral("message")), 240);
    for (auto& character : message) {
        if (character.isNull() || character.category() == QChar::Other_Control)
            character = QLatin1Char(' ');
    }
    message.replace(QLatin1Char('<'), QLatin1Char('['));
    message.replace(QLatin1Char('>'), QLatin1Char(']'));
    return message.simplified();
}

QString safeRequestId(const QByteArray& value)
{
    const QString requestId = QString::fromLatin1(value.left(64));
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9-]{1,64}$"));
    return pattern.match(requestId).hasMatch() ? requestId : QString{};
}

bool trustedLogDownloadUrl(const QUrl& url)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || !url.userInfo().isEmpty()
        || (url.port(-1) != -1 && url.port() != 443) || !url.fragment().isEmpty()) return false;
    const QString host = url.host().toLower();
    return host.endsWith(QStringLiteral(".blob.core.windows.net"))
        || host.endsWith(QStringLiteral(".githubusercontent.com"))
        || host == QStringLiteral("githubusercontent.com");
}

QString oauthErrorMessage(const QString& code)
{
    if (code == QStringLiteral("authorization_pending")) return QObject::tr("Waiting for authorization in your browser.");
    if (code == QStringLiteral("slow_down")) return QObject::tr("GitHub asked DevHub to slow down authorization checks.");
    if (code == QStringLiteral("expired_token") || code == QStringLiteral("token_expired"))
        return QObject::tr("The authorization code expired. Start a new GitHub connection.");
    if (code == QStringLiteral("access_denied")) return QObject::tr("GitHub authorization was denied.");
    if (code == QStringLiteral("incorrect_client_credentials"))
        return QObject::tr("The GitHub App Client ID is not valid. Check the OAuth Client ID in Settings → Integrations → GitHub.");
    if (code == QStringLiteral("device_flow_disabled"))
        return QObject::tr("Device Flow is disabled for this GitHub App. Enable it in the GitHub App settings and try again.");
    if (code == QStringLiteral("incorrect_device_code") || code == QStringLiteral("bad_verification_code"))
        return QObject::tr("GitHub rejected this authorization code. Start a new connection.");
    if (code == QStringLiteral("unsupported_grant_type"))
        return QObject::tr("GitHub did not accept the Device Flow grant. Check the GitHub App configuration and try again.");
    if (code == QStringLiteral("bad_refresh_token"))
        return QObject::tr("The saved GitHub refresh token expired or was revoked. Reconnect GitHub.");
    if (code == QStringLiteral("unverified_user_email"))
        return QObject::tr("Verify your primary email address on GitHub, then connect again.");
    return QObject::tr("GitHub could not complete authorization (%1). Check the GitHub App settings and try again.").arg(code);
}

QUrlQuery formQuery(const QList<QPair<QString, QString>>& values)
{
    QUrlQuery query;
    for (const auto& item : values) query.addQueryItem(item.first, item.second);
    return query;
}

} // namespace

struct GitHubService::TokenResponse final
{
    QString accessToken;
    QString refreshToken;
    std::optional<int> accessExpiresIn;
    std::optional<int> refreshExpiresIn;
    QString error;
};

GitHubService::GitHubService(QString clientId, QString localAccountId, CredentialStore credentialStore,
                             QObject* parent, QNetworkAccessManager* network)
    : QObject(parent), m_clientId(std::move(clientId)), m_localAccountId(std::move(localAccountId)),
      m_credentialStore(std::move(credentialStore)), m_ownedNetwork(),
      m_network(network ? network : &m_ownedNetwork)
{
    Q_ASSERT(QThread::currentThread() == thread());
    qRegisterMetaType<GitHubConnection>();
    qRegisterMetaType<QVector<GitHubPullRequest>>();
    qRegisterMetaType<GitHubPullRequestDetails>();
    qRegisterMetaType<GitHubMonitorSnapshot>();
    m_pollTimer.setSingleShot(true);
    m_pollTimer.setTimerType(Qt::CoarseTimer);
    m_countdownTimer.setInterval(1000);
    m_countdownTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] { pollDeviceCode(m_flowGeneration); });
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        const int remaining = static_cast<int>(QDateTime::currentDateTimeUtc().secsTo(m_deviceExpiresAtUtc));
        emit authorizationTimeRemaining(qMax(remaining, 0));
        if (remaining <= 0 && m_connection.state == GitHubConnection::State::WaitingForAuthorization)
            finishDeviceFlow(m_flowGeneration, false, tr("The authorization code expired. Start a new GitHub connection."));
    });
    setConnection(hasValidClientId() ? GitHubConnection::State::Disconnected
                                     : GitHubConnection::State::NotConfigured);
}

GitHubService::~GitHubService()
{
    cancelDeviceFlow();
    cancelDashboardRequests();
    cancelMonitoringRequests();
    if (m_authReply) m_authReply->abort();
    if (m_flowProfileReply) m_flowProfileReply->abort();
    if (m_authApiReply) m_authApiReply->abort();
}

bool GitHubService::hasValidClientId() const
{
    // GitHub has issued both legacy Iv1.* and newer Iv23* App Client IDs.
    static const QRegularExpression clientIdPattern(QStringLiteral("^Iv(?:1\\.|23)[A-Za-z0-9]+$"));
    return clientIdPattern.match(m_clientId.trimmed()).hasMatch() && m_clientId.size() <= 128;
}

void GitHubService::setClientId(QString clientId)
{
    const QString next = clientId.trimmed();
    if (m_clientId == next) return;
    m_clientId = next;
    if (!m_sessionActive) {
        setConnection(GitHubConnection::State::Locked, tr("Sign in to DevHub to use GitHub."));
        return;
    }
    if (!hasValidClientId()) {
        if (m_hasCredentials) setConnection(GitHubConnection::State::ReauthorizationRequired,
            tr("Configure a valid GitHub App OAuth Client ID to continue this connection."));
        else setConnection(GitHubConnection::State::NotConfigured,
            tr("Enter the OAuth Client ID from your GitHub App settings to enable Device Flow."));
        return;
    }
    restoreCredentials();
}

void GitHubService::setSessionActive(bool active)
{
    if (m_sessionActive == active) return;
    m_sessionActive = active;
    if (!active) {
        cancelDeviceFlow();
        cancelDashboardRequests();
        cancelMonitoringRequests();
        if (m_authReply) m_authReply->abort();
        if (m_authApiReply) m_authApiReply->abort();
        m_authReply = nullptr;
        m_refreshInFlight = false;
        m_refreshWaiters.clear();
        m_hasCredentials = false;
        m_credentials = {};
        setConnection(GitHubConnection::State::Locked, tr("Sign in to DevHub to use GitHub."));
        return;
    }
    restoreCredentials();
}

void GitHubService::beginDeviceFlow()
{
    if (!m_sessionActive) {
        emit deviceFlowFinished(false, tr("Sign in to DevHub before connecting GitHub."));
        return;
    }
    if (!hasValidClientId()) {
        setConnection(GitHubConnection::State::NotConfigured,
            tr("Configure a valid GitHub App OAuth Client ID in Settings → Integrations → GitHub."));
        emit deviceFlowFinished(false, m_connection.message);
        return;
    }
    cancelDeviceFlow();
    const quint64 generation = ++m_flowGeneration;
    m_deviceCode.clear();
    setConnection(GitHubConnection::State::Connecting, tr("Requesting a GitHub authorization code…"));
    const auto form = formQuery({{QStringLiteral("client_id"), m_clientId},
                                 {QStringLiteral("scope"), QStringLiteral("offline_access")}})
                          .toString(QUrl::FullyEncoded).toUtf8();
    requestOAuth(form, QString::fromLatin1(DeviceCodeEndpoint), [this, generation](QJsonDocument document, QString error, int status, QByteArray) {
        if (generation != m_flowGeneration || !m_sessionActive) return;
        if (!error.isEmpty()) {
            finishDeviceFlow(generation, false, error);
            return;
        }
        if (status < 200 || status >= 300 || !document.isObject()) {
            finishDeviceFlow(generation, false, tr("GitHub returned an invalid authorization response."));
            return;
        }
        const auto object = document.object();
        const QString oauthError = object.value(QStringLiteral("error")).toString();
        if (!oauthError.isEmpty()) {
            finishDeviceFlow(generation, false, oauthErrorMessage(oauthError));
            return;
        }
        const QString deviceCode = object.value(QStringLiteral("device_code")).toString();
        const QString userCode = object.value(QStringLiteral("user_code")).toString();
        const QUrl verification(object.value(QStringLiteral("verification_uri")).toString(), QUrl::StrictMode);
        const int expiresIn = object.value(QStringLiteral("expires_in")).toInt(0);
        const int interval = object.value(QStringLiteral("interval")).toInt(5);
        if (deviceCode.isEmpty() || deviceCode.size() > 512 || userCode.isEmpty() || userCode.size() > 64
            || !trustedGitHubUrl(verification.toString()) || expiresIn < 30 || expiresIn > 3600
            || interval < 1 || interval > 60) {
            finishDeviceFlow(generation, false, tr("GitHub returned incomplete or unsafe Device Flow details."));
            return;
        }
        m_deviceCode = deviceCode;
        m_verificationUri = verification;
        m_deviceExpiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
        m_pollIntervalSeconds = interval;
        setConnection(GitHubConnection::State::WaitingForAuthorization,
                      tr("Waiting for authorization in your browser."));
        emit deviceCodeReady(userCode, verification, expiresIn);
        emit authorizationTimeRemaining(expiresIn);
        m_countdownTimer.start();
        m_pollTimer.start(m_pollIntervalSeconds * 1000);
    }, true, generation);
}

void GitHubService::cancelDeviceFlow()
{
    const bool wasActive = m_connection.state == GitHubConnection::State::Connecting
        || m_connection.state == GitHubConnection::State::WaitingForAuthorization;
    ++m_flowGeneration;
    m_pollTimer.stop();
    m_countdownTimer.stop();
    if (m_flowReply) m_flowReply->abort();
    if (m_flowProfileReply) m_flowProfileReply->abort();
    m_flowReply = nullptr;
    m_flowProfileReply = nullptr;
    m_deviceCode.clear();
    if (wasActive) {
        if (!m_sessionActive) setConnection(GitHubConnection::State::Locked, tr("Sign in to DevHub to use GitHub."));
        else if (m_hasCredentials) updateConnectionFromCredentials();
        else setConnection(hasValidClientId() ? GitHubConnection::State::Disconnected
                                              : GitHubConnection::State::NotConfigured);
    }
}

void GitHubService::pollDeviceCode(quint64 generation)
{
    if (generation != m_flowGeneration || !m_sessionActive || m_deviceCode.isEmpty()) return;
    if (QDateTime::currentDateTimeUtc() >= m_deviceExpiresAtUtc) {
        finishDeviceFlow(generation, false, tr("The authorization code expired. Start a new GitHub connection."));
        return;
    }
    exchangeDeviceToken(generation);
}

void GitHubService::exchangeDeviceToken(quint64 generation)
{
    const auto form = formQuery({
        {QStringLiteral("client_id"), m_clientId},
        {QStringLiteral("device_code"), m_deviceCode},
        {QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")}})
        .toString(QUrl::FullyEncoded).toUtf8();
    requestOAuth(form, QString::fromLatin1(TokenEndpoint), [this, generation](QJsonDocument document, QString error, int status, QByteArray) {
        if (generation != m_flowGeneration || !m_sessionActive) return;
        if (!error.isEmpty()) {
            finishDeviceFlow(generation, false, error);
            return;
        }
        if (status < 200 || status >= 300 || !document.isObject()) {
            finishDeviceFlow(generation, false, tr("GitHub returned an invalid token response."));
            return;
        }
        QString errorCode;
        const auto token = parseTokenResponse(document, errorCode);
        if (!token) {
            if (errorCode == QStringLiteral("authorization_pending")) {
                setConnection(GitHubConnection::State::WaitingForAuthorization,
                              tr("Waiting for authorization in your browser."));
                m_pollTimer.start(m_pollIntervalSeconds * 1000);
                return;
            }
            if (errorCode == QStringLiteral("slow_down")) {
                m_pollIntervalSeconds = qMax(m_pollIntervalSeconds + 5,
                    document.object().value(QStringLiteral("interval")).toInt(m_pollIntervalSeconds + 5));
                m_pollIntervalSeconds = qMin(m_pollIntervalSeconds, 3600);
                setConnection(GitHubConnection::State::WaitingForAuthorization,
                              tr("GitHub requested a slower authorization check. Waiting %1 seconds.").arg(m_pollIntervalSeconds));
                m_pollTimer.start(m_pollIntervalSeconds * 1000);
                return;
            }
            finishDeviceFlow(generation, false, oauthErrorMessage(errorCode.isEmpty()
                ? QStringLiteral("malformed_response") : errorCode));
            return;
        }
        GitHubCredentials candidate;
        candidate.accessToken = token->accessToken;
        candidate.clientId = m_clientId;
        candidate.refreshToken = token->refreshToken;
        const auto now = QDateTime::currentDateTimeUtc();
        if (token->accessExpiresIn) candidate.accessTokenExpiresAtUtc = now.addSecs(*token->accessExpiresIn);
        if (token->refreshExpiresIn) candidate.refreshTokenExpiresAtUtc = now.addSecs(*token->refreshExpiresIn);
        // Copy before moving candidate into the callback; function argument evaluation order is unspecified.
        const QString accessToken = candidate.accessToken;
        verifyUser(accessToken,
            [this, generation, candidate = std::move(candidate)](bool ok, QString login, QString avatar, QString message) mutable {
                if (generation != m_flowGeneration || !m_sessionActive) return;
                if (!ok) {
                    finishDeviceFlow(generation, false, message);
                    return;
                }
                candidate.login = std::move(login);
                candidate.avatarUrl = std::move(avatar);
                candidate.lastVerifiedAtUtc = QDateTime::currentDateTimeUtc();
                QString storageError;
                if (!persistCredentials(candidate, storageError)) {
                    candidate.accessToken.clear();
                    candidate.refreshToken.clear();
                    finishDeviceFlow(generation, false, storageError);
                    return;
                }
                m_credentials = std::move(candidate);
                m_hasCredentials = true;
                setConnection(GitHubConnection::State::Connected,
                              tr("Connected to GitHub as %1.").arg(m_credentials.login));
                finishDeviceFlow(generation, true, m_connection.message);
            }, true, generation);
    }, true, generation);
}

void GitHubService::finishDeviceFlow(quint64 generation, bool succeeded, QString message)
{
    if (generation != m_flowGeneration) return;
    m_pollTimer.stop();
    m_countdownTimer.stop();
    if (m_flowReply) m_flowReply->abort();
    if (m_flowProfileReply) m_flowProfileReply->abort();
    m_flowReply = nullptr;
    m_flowProfileReply = nullptr;
    m_deviceCode.clear();
    if (!succeeded) {
        if (message.contains(tr("Client ID"), Qt::CaseInsensitive)
            && m_hasCredentials) setConnection(GitHubConnection::State::ReauthorizationRequired, message);
        else if (m_hasCredentials) updateConnectionFromCredentials();
        else setConnection(hasValidClientId() ? GitHubConnection::State::Error
                                              : GitHubConnection::State::NotConfigured, message);
    }
    emit deviceFlowFinished(succeeded, std::move(message));
}

void GitHubService::verifyUser(QString accessToken,
                               std::function<void(bool, QString, QString, QString)> completion,
                               bool deviceFlowRequest, quint64 flowGeneration)
{
    requestApiWithToken(QStringLiteral("/user"), std::move(accessToken),
        [this, completion = std::move(completion)](QJsonDocument document, QString error, int status, QByteArray) mutable {
            if (!error.isEmpty()) { completion(false, {}, {}, error); return; }
            if (status != 200 || !document.isObject()) {
                completion(false, {}, {}, status == 401
                    ? tr("GitHub rejected the access token. Reconnect GitHub and try again.")
                    : tr("DevHub could not verify the GitHub account."));
                return;
            }
            const auto object = document.object();
            const QString login = boundedText(object.value(QStringLiteral("login")), 100);
            const QString avatar = boundedText(object.value(QStringLiteral("avatar_url")), 2048);
            if (login.isEmpty() || (object.contains(QStringLiteral("avatar_url")) && !avatar.isEmpty()
                                    && !avatar.startsWith(QStringLiteral("https://avatars.githubusercontent.com/")))) {
                completion(false, {}, {}, tr("GitHub returned an invalid account profile."));
                return;
            }
            completion(true, login, avatar, {});
        }, deviceFlowRequest, flowGeneration, !deviceFlowRequest);
}

void GitHubService::restoreCredentials()
{
    if (!m_sessionActive) return;
    if (!hasValidClientId()) {
        setConnection(GitHubConnection::State::NotConfigured,
            tr("Configure the GitHub App OAuth Client ID in Settings → Integrations → GitHub."));
        return;
    }
    if (!m_credentialStore || m_localAccountId.isEmpty()) {
        setConnection(GitHubConnection::State::Error,
            tr("Secure GitHub credential storage is unavailable for this DevHub account."));
        return;
    }
    GitHubCredentials restored;
    QString error;
    if (!m_credentialStore->load(m_localAccountId, restored, error)) {
        setConnection(GitHubConnection::State::Error, error);
        return;
    }
    if (restored.accessToken.isEmpty()) {
        m_credentials = {};
        m_hasCredentials = false;
        setConnection(GitHubConnection::State::Disconnected,
                      tr("GitHub is not connected for this DevHub account."));
        return;
    }
    m_credentials = std::move(restored);
    m_hasCredentials = true;
    if (m_credentials.clientId != m_clientId) {
        setConnection(GitHubConnection::State::ReauthorizationRequired,
            tr("The configured GitHub App Client ID changed. Reconnect GitHub to authorize this app."));
        return;
    }
    recheckConnection();
}

void GitHubService::recheckConnection()
{
    if (!m_sessionActive) {
        setConnection(GitHubConnection::State::Locked, tr("Sign in to DevHub to use GitHub."));
        return;
    }
    if (!hasValidClientId()) {
        setConnection(GitHubConnection::State::NotConfigured,
            tr("Configure the GitHub App OAuth Client ID in Settings → Integrations → GitHub."));
        return;
    }
    if (!m_hasCredentials) {
        restoreCredentials();
        return;
    }
    if (m_credentials.clientId != m_clientId) {
        setConnection(GitHubConnection::State::ReauthorizationRequired,
            tr("The configured GitHub App Client ID changed. Reconnect GitHub to authorize this app."));
        return;
    }
    setConnection(GitHubConnection::State::Verifying, tr("Checking the saved GitHub connection…"));
    ensureValidToken([this](bool ok, QString error) {
        if (!m_sessionActive) return;
        if (!ok) {
            setConnection(error.contains(tr("refresh token"), Qt::CaseInsensitive)
                ? GitHubConnection::State::ReauthorizationRequired : GitHubConnection::State::Error, error);
            return;
        }
        verifyUser(m_credentials.accessToken, [this](bool valid, QString login, QString avatar, QString errorText) {
            if (!m_sessionActive) return;
            if (!valid) {
                setConnection(GitHubConnection::State::ReauthorizationRequired,
                              errorText.isEmpty() ? tr("GitHub authorization expired. Reconnect GitHub.") : errorText);
                return;
            }
            m_credentials.login = std::move(login);
            m_credentials.avatarUrl = std::move(avatar);
            m_credentials.lastVerifiedAtUtc = QDateTime::currentDateTimeUtc();
            QString saveError;
            if (!persistCredentials(m_credentials, saveError)) {
                setConnection(GitHubConnection::State::Error, saveError);
                return;
            }
            setConnection(GitHubConnection::State::Connected,
                          tr("Connected to GitHub as %1.").arg(m_credentials.login));
        });
    });
}

void GitHubService::ensureValidToken(std::function<void(bool, QString)> completion)
{
    if (!m_sessionActive || !m_hasCredentials || m_credentials.accessToken.isEmpty()) {
        completion(false, tr("Connect GitHub to use the Pull Request dashboard."));
        return;
    }
    if (m_connection.state == GitHubConnection::State::ReauthorizationRequired) {
        completion(false, m_connection.message.isEmpty()
            ? tr("Reconnect GitHub to continue.") : m_connection.message);
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    if (!m_credentials.accessTokenExpiresAtUtc.isValid()
        || m_credentials.accessTokenExpiresAtUtc > now.addSecs(120)) {
        completion(true, {});
        return;
    }
    if (m_credentials.refreshToken.isEmpty()
        || (m_credentials.refreshTokenExpiresAtUtc.isValid() && m_credentials.refreshTokenExpiresAtUtc <= now)) {
        const QString error = tr("The GitHub access token expired and no valid refresh token is available. Reconnect GitHub.");
        setConnection(GitHubConnection::State::ReauthorizationRequired, error);
        completion(false, error);
        return;
    }
    refreshAccessToken(std::move(completion));
}

void GitHubService::refreshAccessToken(std::function<void(bool, QString)> completion)
{
    if (m_refreshInFlight) {
        m_refreshWaiters.push_back(std::move(completion));
        return;
    }
    if (m_credentials.refreshToken.isEmpty()) {
        completion(false, tr("The saved GitHub refresh token is missing. Reconnect GitHub."));
        return;
    }
    m_refreshInFlight = true;
    m_refreshWaiters.push_back(std::move(completion));
    const auto form = formQuery({
        {QStringLiteral("client_id"), m_clientId},
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
        {QStringLiteral("refresh_token"), m_credentials.refreshToken}})
        .toString(QUrl::FullyEncoded).toUtf8();
    requestOAuth(form, QString::fromLatin1(TokenEndpoint), [this](QJsonDocument document, QString error, int status, QByteArray) {
        if (!m_sessionActive || !m_hasCredentials) {
            m_refreshInFlight = false;
            m_refreshWaiters.clear();
            return;
        }
        QString failure;
        bool success = false;
        if (!error.isEmpty()) failure = error;
        else if (status < 200 || status >= 300 || !document.isObject())
            failure = tr("GitHub returned an invalid refresh response.");
        else {
            QString oauthError;
            const auto token = parseTokenResponse(document, oauthError);
            if (!token) {
                failure = oauthError == QStringLiteral("bad_refresh_token")
                    ? oauthErrorMessage(oauthError) : tr("GitHub could not refresh the saved connection.");
            } else {
                GitHubCredentials replacement = m_credentials;
                replacement.accessToken = token->accessToken;
                if (!token->refreshToken.isEmpty()) replacement.refreshToken = token->refreshToken;
                const auto now = QDateTime::currentDateTimeUtc();
                replacement.accessTokenExpiresAtUtc = token->accessExpiresIn
                    ? now.addSecs(*token->accessExpiresIn) : QDateTime{};
                if (token->refreshExpiresIn) replacement.refreshTokenExpiresAtUtc = now.addSecs(*token->refreshExpiresIn);
                QString storeError;
                if (!persistCredentials(replacement, storeError)) failure = storeError;
                else {
                    m_credentials = std::move(replacement);
                    success = true;
                }
            }
        }
        m_refreshInFlight = false;
        auto waiters = std::move(m_refreshWaiters);
        m_refreshWaiters.clear();
        if (!success && failure.contains(tr("refresh token"), Qt::CaseInsensitive))
            setConnection(GitHubConnection::State::ReauthorizationRequired, failure);
        for (auto& waiter : waiters) waiter(success, failure);
    });
}

void GitHubService::requestOAuth(QByteArray form, QString endpoint, JsonCallback completion, bool deviceFlowRequest,
                                 quint64 flowGeneration)
{
    QNetworkRequest request{QUrl(std::move(endpoint))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(RequestTimeoutMs);
    auto* reply = m_network->post(request, std::move(form));
    if (deviceFlowRequest) m_flowReply = reply;
    else m_authReply = reply;
    auto body = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, body, tooLarge] {
        if (*tooLarge) { reply->readAll(); return; }
        body->append(reply->readAll());
        if (body->size() > OAuthResponseLimit) {
            *tooLarge = true;
            body->clear();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, body, tooLarge, completion = std::move(completion), deviceFlowRequest,
         flowGeneration]() mutable {
            if (deviceFlowRequest && flowGeneration != m_flowGeneration) { reply->deleteLater(); return; }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto errorCode = reply->error();
            QString error;
            if (*tooLarge) error = tr("GitHub returned an authorization response that was too large.");
            else if (errorCode != QNetworkReply::NoError && status == 0)
                error = tr("DevHub could not reach GitHub. Check your network connection and try again.");
            else if (body->isEmpty()) error = tr("GitHub returned an empty response.");
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(*body, &parseError);
            if (error.isEmpty() && (parseError.error != QJsonParseError::NoError || !document.isObject()))
                error = tr("GitHub returned a malformed authorization response.");
            const QByteArray contentType = reply->rawHeader("Content-Type");
            body->fill('\0');
            if (deviceFlowRequest) {
                if (m_flowReply == reply) m_flowReply = nullptr;
            } else if (m_authReply == reply) m_authReply = nullptr;
            reply->deleteLater();
            if (!error.isEmpty()) completion({}, error, status, contentType);
            else completion(document, {}, status, contentType);
        });
}

void GitHubService::requestApi(QString path, JsonCallback completion, bool deviceFlowRequest,
                               quint64 flowGeneration, bool monitorRequest)
{
    ensureValidToken([this, path = std::move(path), completion = std::move(completion),
                      deviceFlowRequest, flowGeneration, monitorRequest](bool ok, QString error) mutable {
        if (deviceFlowRequest && flowGeneration != m_flowGeneration) return;
        if (!ok) { completion({}, error, 0, {}); return; }
        requestApiWithToken(std::move(path), m_credentials.accessToken, std::move(completion),
                            deviceFlowRequest, flowGeneration, false, monitorRequest);
    });
}

void GitHubService::requestApiWithToken(QString path, QString accessToken, JsonCallback completion,
                                       bool deviceFlowRequest, quint64 flowGeneration, bool authRequest,
                                       bool monitorRequest)
{
    QUrl url(QString::fromLatin1(ApiRoot) + path);
    if (!url.isValid() || url.scheme() != QStringLiteral("https")
        || url.host() != QStringLiteral("api.github.com")) {
        completion({}, tr("DevHub rejected an unsafe GitHub API address."), 0, {});
        return;
    }
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", ApiVersion);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
    request.setTransferTimeout(RequestTimeoutMs);
    performRequest(std::move(request), "GET", {}, std::move(completion), ApiResponseLimit,
                   deviceFlowRequest, flowGeneration, authRequest, monitorRequest);
}

void GitHubService::performRequest(QNetworkRequest request, QByteArray method, QByteArray payload,
                                   JsonCallback completion, qsizetype responseLimit,
                                   bool deviceFlowRequest, quint64 flowGeneration, bool authRequest,
                                   bool monitorRequest)
{
    auto* reply = m_network->sendCustomRequest(request, method, std::move(payload));
    if (deviceFlowRequest) m_flowProfileReply = reply;
    else if (authRequest) m_authApiReply = reply;
    else if (monitorRequest) m_monitorReplies.push_back(reply);
    else m_dashboardReplies.push_back(reply);
    auto body = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, body, tooLarge, responseLimit] {
        if (*tooLarge) { reply->readAll(); return; }
        body->append(reply->readAll());
        if (body->size() > responseLimit) {
            *tooLarge = true;
            body->clear();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, body, tooLarge, completion = std::move(completion), deviceFlowRequest,
         flowGeneration, authRequest, monitorRequest]() mutable {
            if (deviceFlowRequest && flowGeneration != m_flowGeneration) { reply->deleteLater(); return; }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto errorCode = reply->error();
            QString error;
            if (*tooLarge) error = tr("GitHub returned a response larger than DevHub's safe display limit.");
            else if (errorCode != QNetworkReply::NoError && status == 0)
                error = tr("DevHub could not reach GitHub. Check your network connection and try again.");
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(*body, &parseError);
            if (error.isEmpty() && status >= 200 && status < 300
                && (parseError.error != QJsonParseError::NoError || document.isNull()))
                error = tr("GitHub returned malformed JSON.");
            if (error.isEmpty() && (status == 429 || (status == 403 && reply->rawHeader("X-RateLimit-Remaining") == "0"))) {
                const QByteArray reset = reply->rawHeader("X-RateLimit-Reset");
                const QByteArray retryAfter = reply->rawHeader("Retry-After");
                if (!retryAfter.isEmpty()) {
                    bool secondsOk = false;
                    const qint64 seconds = retryAfter.toLongLong(&secondsOk);
                    error = secondsOk && seconds >= 0 && seconds <= 86400
                        ? tr("GitHub asked DevHub to wait %1 seconds before refreshing.").arg(seconds)
                        : tr("GitHub asked DevHub to wait before refreshing. Try again later.");
                } else if (!reset.isEmpty()) {
                    error = tr("GitHub's API rate limit was reached. Try again after %1.")
                        .arg(QDateTime::fromSecsSinceEpoch(reset.toLongLong(), QTimeZone::UTC).toLocalTime().toString());
                } else error = tr("GitHub's API rate limit was reached. Wait a while before refreshing.");
            } else if (error.isEmpty() && status == 401) {
                const QString stage = deviceFlowRequest
                    ? tr("GitHub approved the device, but DevHub could not verify the issued token (GET /user, HTTP 401).")
                    : tr("GitHub rejected the saved authorization (HTTP 401). Recheck or reconnect the account.");
                const QString apiMessage = safeApiErrorMessage(document);
                const QString requestId = safeRequestId(reply->rawHeader("X-GitHub-Request-Id"));
                error = stage;
                if (!apiMessage.isEmpty()) error += tr(" GitHub response: %1.").arg(apiMessage);
                if (!requestId.isEmpty()) error += tr(" Request ID: %1.").arg(requestId);
                setConnection(GitHubConnection::State::ReauthorizationRequired, error);
            } else if (error.isEmpty() && (status == 403 || status == 404)) {
                error = tr("GitHub denied access to this repository. Check the App's repository access and read permissions.");
            } else if (error.isEmpty() && errorCode != QNetworkReply::NoError && status == 0)
                error = tr("The GitHub API request failed.");
            const QByteArray link = reply->rawHeader("Link");
            if (monitorRequest) {
                for (auto it = m_monitorReplies.begin(); it != m_monitorReplies.end();) {
                    if (!*it || *it == reply) it = m_monitorReplies.erase(it); else ++it;
                }
            } else if (!deviceFlowRequest && !authRequest) {
                for (auto it = m_dashboardReplies.begin(); it != m_dashboardReplies.end();) {
                    if (!*it || *it == reply) it = m_dashboardReplies.erase(it); else ++it;
                }
            } else if (deviceFlowRequest && m_flowProfileReply == reply) m_flowProfileReply = nullptr;
            else if (authRequest && m_authApiReply == reply) m_authApiReply = nullptr;
            reply->deleteLater();
            if (!error.isEmpty()) completion({}, error, status, link);
            else completion(document, {}, status, link);
        });
}

void GitHubService::disconnectGitHub()
{
    cancelDeviceFlow();
    cancelDashboardRequests();
    if (m_authReply) m_authReply->abort();
    m_authReply = nullptr;
    QString error;
    if (m_credentialStore && !m_credentialStore->remove(m_localAccountId, error)) {
        setConnection(GitHubConnection::State::Error, error);
        return;
    }
    m_credentials = {};
    m_hasCredentials = false;
    setConnection(hasValidClientId() ? GitHubConnection::State::Disconnected
                                    : GitHubConnection::State::NotConfigured,
                  tr("GitHub was disconnected from this DevHub account. The GitHub authorization itself remains active until revoked on GitHub."));
}

void GitHubService::cancelDashboardRequests()
{
    ++m_dashboardGeneration;
    for (auto& reply : m_dashboardReplies) if (reply) reply->abort();
    m_dashboardReplies.clear();
}

void GitHubService::cancelMonitoringRequests()
{
    ++m_monitorGeneration;
    for (auto& reply : m_monitorReplies) if (reply) reply->abort();
    m_monitorReplies.clear();
}

void GitHubService::loadPullRequests(const GitHubRepository& repository, QString state, int page)
{
    const QString pathRoot = repoPath(repository);
    if (pathRoot.isEmpty() || !QStringList{QStringLiteral("open"), QStringLiteral("closed"), QStringLiteral("all")}.contains(state)) {
        emit pullRequestsLoaded({}, false, page, tr("The selected GitHub repository or filter is invalid."));
        return;
    }
    const quint64 generation = m_dashboardGeneration;
    QUrl url(QString::fromLatin1(ApiRoot) + pathRoot + QStringLiteral("/pulls"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("state"), state);
    query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("50"));
    query.addQueryItem(QStringLiteral("page"), QString::number(qBound(1, page, 1000)));
    query.addQueryItem(QStringLiteral("sort"), QStringLiteral("updated"));
    query.addQueryItem(QStringLiteral("direction"), QStringLiteral("desc"));
    url.setQuery(query);
    requestApi(url.toString(QUrl::FullyEncoded).mid(QString::fromLatin1(ApiRoot).size()),
        [this, generation, page](QJsonDocument document, QString error, int status, QByteArray link) {
            if (generation != m_dashboardGeneration) return;
            if (!error.isEmpty() || status != 200 || !document.isArray()) {
                emit pullRequestsLoaded({}, false, page, error.isEmpty()
                    ? tr("GitHub returned an invalid Pull Request list.") : error);
                return;
            }
            QVector<GitHubPullRequest> pullRequests;
            const QJsonArray array = document.array();
            pullRequests.reserve(qMin(array.size(), 50));
            for (const auto& value : array) {
                if (!value.isObject()) continue;
                const auto object = value.toObject();
                const auto head = object.value(QStringLiteral("head")).toObject();
                const auto base = object.value(QStringLiteral("base")).toObject();
                const auto user = object.value(QStringLiteral("user")).toObject();
                GitHubPullRequest item;
                item.number = object.value(QStringLiteral("number")).toInt();
                item.title = boundedText(object.value(QStringLiteral("title")), 4096);
                item.author = boundedText(user.value(QStringLiteral("login")), 100);
                item.headBranch = boundedText(head.value(QStringLiteral("ref")), 256);
                item.baseBranch = boundedText(base.value(QStringLiteral("ref")), 256);
                item.state = boundedText(object.value(QStringLiteral("state")), 32);
                item.draft = object.value(QStringLiteral("draft")).toBool();
                item.merged = object.value(QStringLiteral("merged_at")).isString();
                item.createdAtUtc = dateValue(object.value(QStringLiteral("created_at")));
                item.updatedAtUtc = dateValue(object.value(QStringLiteral("updated_at")));
                item.htmlUrl = boundedText(object.value(QStringLiteral("html_url")), 2048);
                item.headSha = boundedText(head.value(QStringLiteral("sha")), 64);
                item.baseSha = boundedText(base.value(QStringLiteral("sha")), 64);
                if (item.number > 0 && !item.title.isEmpty() && validGitHubWebUrl(item.htmlUrl))
                    pullRequests.push_back(std::move(item));
            }
            const bool hasMore = link.contains("rel=\"next\"");
            emit pullRequestsLoaded(std::move(pullRequests), hasMore, page, {});
        });
}

void GitHubService::loadPullRequestDetails(const GitHubRepository& repository, int number)
{
    const QString root = repoPath(repository);
    if (root.isEmpty() || number < 1) {
        emit pullRequestDetailsLoaded({}, tr("The selected Pull Request is invalid."));
        return;
    }
    const quint64 generation = m_dashboardGeneration;
    requestApi(root + QStringLiteral("/pulls/%1").arg(number),
        [this, generation, repository, root](QJsonDocument document, QString error, int status, QByteArray) {
            if (generation != m_dashboardGeneration) return;
            if (!error.isEmpty() || status != 200 || !document.isObject()) {
                emit pullRequestDetailsLoaded({}, error.isEmpty()
                    ? tr("GitHub returned invalid Pull Request details.") : error);
                return;
            }
            const auto object = document.object();
            const auto head = object.value(QStringLiteral("head")).toObject();
            const auto base = object.value(QStringLiteral("base")).toObject();
            const auto user = object.value(QStringLiteral("user")).toObject();
            auto details = std::make_shared<GitHubPullRequestDetails>();
            auto& pr = details->pullRequest;
            pr.number = object.value(QStringLiteral("number")).toInt();
            pr.title = boundedText(object.value(QStringLiteral("title")), 4096);
            pr.author = boundedText(user.value(QStringLiteral("login")), 100);
            pr.headBranch = boundedText(head.value(QStringLiteral("ref")), 256);
            pr.baseBranch = boundedText(base.value(QStringLiteral("ref")), 256);
            pr.state = boundedText(object.value(QStringLiteral("state")), 32);
            pr.draft = object.value(QStringLiteral("draft")).toBool();
            pr.merged = object.value(QStringLiteral("merged_at")).isString();
            pr.createdAtUtc = dateValue(object.value(QStringLiteral("created_at")));
            pr.updatedAtUtc = dateValue(object.value(QStringLiteral("updated_at")));
            pr.htmlUrl = boundedText(object.value(QStringLiteral("html_url")), 2048);
            pr.body = boundedText(object.value(QStringLiteral("body")), 256 * 1024);
            pr.headSha = boundedText(head.value(QStringLiteral("sha")), 64);
            pr.baseSha = boundedText(base.value(QStringLiteral("sha")), 64);
            pr.mergeableState = boundedText(object.value(QStringLiteral("mergeable_state")), 64);
            pr.additions = object.value(QStringLiteral("additions")).toInt();
            pr.deletions = object.value(QStringLiteral("deletions")).toInt();
            pr.changedFiles = object.value(QStringLiteral("changed_files")).toInt();
            if (!validGitHubWebUrl(pr.htmlUrl) || pr.number <= 0) {
                emit pullRequestDetailsLoaded({}, tr("GitHub returned invalid Pull Request data."));
                return;
            }
            QStringList paths{root + QStringLiteral("/pulls/%1/files?per_page=100&page=1").arg(pr.number),
                              root + QStringLiteral("/pulls/%1/reviews?per_page=100&page=1").arg(pr.number),
                              root + QStringLiteral("/issues/%1/comments?per_page=100&page=1").arg(pr.number)};
            const bool shaValid = QRegularExpression(QStringLiteral("^[A-Fa-f0-9]{40,64}$")).match(pr.headSha).hasMatch();
            if (shaValid) {
                paths << root + QStringLiteral("/commits/%1/check-runs?per_page=100").arg(pr.headSha);
                paths << root + QStringLiteral("/actions/runs?head_sha=%1&per_page=100").arg(pr.headSha);
            }
            auto remaining = std::make_shared<int>(5);
            auto finishOne = [this, details, remaining, generation](QString errorText) {
                if (!errorText.isEmpty()) details->partialErrors.push_back(std::move(errorText));
                --*remaining;
                if (*remaining == 0 && generation == m_dashboardGeneration)
                    emit pullRequestDetailsLoaded(std::move(*details), {});
            };
            requestApi(paths.at(0), [details, finishOne](QJsonDocument data, QString errorText, int code, QByteArray) mutable {
                if (errorText.isEmpty() && code == 200 && data.isArray()) {
                    const auto array = data.array();
                    for (const auto& value : array) {
                        if (!value.isObject()) continue;
                        const auto object = value.toObject();
                        GitHubPullRequestFile file;
                        file.path = boundedText(object.value(QStringLiteral("filename")), 2048);
                        file.status = boundedText(object.value(QStringLiteral("status")), 64);
                        file.additions = object.value(QStringLiteral("additions")).toInt();
                        file.deletions = object.value(QStringLiteral("deletions")).toInt();
                        file.patch = boundedText(object.value(QStringLiteral("patch")), 128 * 1024);
                        file.patchUnavailable = !object.value(QStringLiteral("patch")).isString();
                        if (!file.path.isEmpty()) details->files.push_back(std::move(file));
                    }
                } else if (errorText.isEmpty()) errorText = QObject::tr("Changed files were not available.");
                finishOne(std::move(errorText));
            });
            requestApi(paths.at(1), [details, finishOne](QJsonDocument data, QString errorText, int code, QByteArray) mutable {
                if (errorText.isEmpty() && code == 200 && data.isArray()) {
                    for (const auto& value : data.array()) {
                        if (!value.isObject()) continue;
                        const auto object = value.toObject();
                        GitHubReview review;
                        review.reviewer = boundedText(object.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")), 100);
                        review.state = boundedText(object.value(QStringLiteral("state")), 64);
                        review.body = boundedText(object.value(QStringLiteral("body")), 64 * 1024);
                        review.submittedAtUtc = dateValue(object.value(QStringLiteral("submitted_at")));
                        details->reviews.push_back(std::move(review));
                    }
                } else if (errorText.isEmpty()) errorText = QObject::tr("Review information was not available.");
                finishOne(std::move(errorText));
            });
            requestApi(paths.at(2), [details, finishOne](QJsonDocument data, QString errorText, int code, QByteArray) mutable {
                if (errorText.isEmpty() && code == 200 && data.isArray()) {
                    for (const auto& value : data.array()) {
                        if (!value.isObject()) continue;
                        const auto object = value.toObject();
                        GitHubDiscussionComment comment;
                        comment.author = boundedText(object.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")), 100);
                        comment.body = boundedText(object.value(QStringLiteral("body")), 64 * 1024);
                        comment.createdAtUtc = dateValue(object.value(QStringLiteral("created_at")));
                        details->comments.push_back(std::move(comment));
                    }
                } else if (errorText.isEmpty()) errorText = QObject::tr("Discussion comments were not available.");
                finishOne(std::move(errorText));
            });
            if (shaValid) {
                requestApi(paths.at(3), [details, finishOne](QJsonDocument data, QString errorText, int code, QByteArray) mutable {
                    if (errorText.isEmpty() && code == 200 && data.isObject()) {
                        for (const auto& value : data.object().value(QStringLiteral("check_runs")).toArray()) {
                            if (!value.isObject()) continue;
                            const auto object = value.toObject();
                            GitHubCheckRun check;
                            check.name = boundedText(object.value(QStringLiteral("name")), 256);
                            check.status = boundedText(object.value(QStringLiteral("status")), 64);
                            check.conclusion = boundedText(object.value(QStringLiteral("conclusion")), 64);
                            check.headSha = boundedText(object.value(QStringLiteral("head_sha")), 64);
                            check.htmlUrl = boundedText(object.value(QStringLiteral("html_url")), 2048);
                            check.startedAtUtc = dateValue(object.value(QStringLiteral("started_at")));
                            check.completedAtUtc = dateValue(object.value(QStringLiteral("completed_at")));
                            details->checks.push_back(std::move(check));
                        }
                    } else if (errorText.isEmpty()) errorText = QObject::tr("Check runs were not available.");
                    finishOne(std::move(errorText));
                });
                requestApi(paths.at(4), [this, details, finishOne, repository, remaining](QJsonDocument data, QString errorText, int code, QByteArray) mutable {
                    QVector<qint64> failedRunIds;
                    if (errorText.isEmpty() && code == 200 && data.isObject()) {
                        const auto runs = data.object().value(QStringLiteral("workflow_runs")).toArray();
                        for (const auto& value : runs) {
                            if (!value.isObject()) continue;
                            const auto object = value.toObject();
                            GitHubWorkflowRun run;
                            run.id = object.value(QStringLiteral("id")).toVariant().toLongLong();
                            run.name = boundedText(object.value(QStringLiteral("name")), 256);
                            run.status = boundedText(object.value(QStringLiteral("status")), 64);
                            run.conclusion = boundedText(object.value(QStringLiteral("conclusion")), 64);
                            run.headSha = boundedText(object.value(QStringLiteral("head_sha")), 64);
                            run.htmlUrl = boundedText(object.value(QStringLiteral("html_url")), 2048);
                            run.startedAtUtc = dateValue(object.value(QStringLiteral("run_started_at")));
                            if (!run.startedAtUtc.isValid()) run.startedAtUtc = dateValue(object.value(QStringLiteral("created_at")));
                            run.completedAtUtc = dateValue(object.value(QStringLiteral("updated_at")));
                            details->workflowRuns.push_back(run);
                            const qint64 id = run.id;
                            if (id > 0 && run.conclusion == QStringLiteral("failure") && failedRunIds.size() < 5)
                                failedRunIds.push_back(id);
                        }
                        *remaining += static_cast<int>(failedRunIds.size());
                        for (const qint64 id : failedRunIds) {
                            const QString jobsPath = repoPath(repository)
                                + QStringLiteral("/actions/runs/%1/jobs?per_page=100").arg(id);
                            requestApi(jobsPath, [details, finishOne](QJsonDocument jobsData, QString jobsError,
                                                                      int jobsCode, QByteArray) mutable {
                                if (jobsError.isEmpty() && jobsCode == 200 && jobsData.isObject()) {
                                    for (const auto& jobValue : jobsData.object().value(QStringLiteral("jobs")).toArray()) {
                                        if (!jobValue.isObject()) continue;
                                        const auto jobObject = jobValue.toObject();
                                        GitHubWorkflowJob job;
                                        job.id = jobObject.value(QStringLiteral("id")).toVariant().toLongLong();
                                        job.name = boundedText(jobObject.value(QStringLiteral("name")), 256);
                                        job.status = boundedText(jobObject.value(QStringLiteral("status")), 64);
                                        job.conclusion = boundedText(jobObject.value(QStringLiteral("conclusion")), 64);
                                        job.htmlUrl = boundedText(jobObject.value(QStringLiteral("html_url")), 2048);
                                        job.startedAtUtc = dateValue(jobObject.value(QStringLiteral("started_at")));
                                        job.completedAtUtc = dateValue(jobObject.value(QStringLiteral("completed_at")));
                                        for (const auto& stepValue : jobObject.value(QStringLiteral("steps")).toArray()) {
                                            if (!stepValue.isObject()) continue;
                                            const auto step = stepValue.toObject();
                                            if (step.value(QStringLiteral("conclusion")).toString() == QStringLiteral("failure"))
                                                job.failedSteps.push_back(boundedText(step.value(QStringLiteral("name")), 256));
                                        }
                                        if (job.conclusion == QStringLiteral("failure")) details->failedJobs.push_back(std::move(job));
                                    }
                                } else if (jobsError.isEmpty()) jobsError = QObject::tr("Failed job information was not available.");
                                finishOne(std::move(jobsError));
                            });
                        }
                    } else if (errorText.isEmpty()) errorText = QObject::tr("GitHub Actions history was not available.");
                    finishOne(std::move(errorText));
                });
            } else {
                finishOne(tr("The Pull Request head commit could not be verified; check runs were not loaded."));
                finishOne(tr("The Pull Request head commit could not be verified; Actions runs were not loaded."));
            }
        });
}

void GitHubService::loadWorkflowRunJobs(const GitHubRepository& repository, qint64 runId)
{
    const QString root = repoPath(repository);
    if (root.isEmpty() || runId <= 0) {
        emit workflowRunJobsLoaded(runId, {}, tr("The selected workflow run is invalid."));
        return;
    }
    const quint64 generation = m_dashboardGeneration;
    requestApi(root + QStringLiteral("/actions/runs/%1/jobs?per_page=100").arg(runId),
        [this, generation, runId](QJsonDocument document, QString error, int status, QByteArray) {
            if (generation != m_dashboardGeneration) return;
            QVector<GitHubWorkflowJob> jobs;
            if (error.isEmpty() && status == 200 && document.isObject()) {
                for (const auto& value : document.object().value(QStringLiteral("jobs")).toArray()) {
                    if (!value.isObject()) continue;
                    const auto object = value.toObject();
                    GitHubWorkflowJob job;
                    job.id = object.value(QStringLiteral("id")).toVariant().toLongLong();
                    job.name = boundedText(object.value(QStringLiteral("name")), 256);
                    job.status = boundedText(object.value(QStringLiteral("status")), 64);
                    job.conclusion = boundedText(object.value(QStringLiteral("conclusion")), 64);
                    job.htmlUrl = boundedText(object.value(QStringLiteral("html_url")), 2048);
                    job.startedAtUtc = dateValue(object.value(QStringLiteral("started_at")));
                    job.completedAtUtc = dateValue(object.value(QStringLiteral("completed_at")));
                    for (const auto& stepValue : object.value(QStringLiteral("steps")).toArray()) {
                        const auto step = stepValue.toObject();
                        if (step.value(QStringLiteral("conclusion")).toString() == QStringLiteral("failure"))
                            job.failedSteps.push_back(boundedText(step.value(QStringLiteral("name")), 256));
                    }
                    jobs.push_back(std::move(job));
                }
            } else if (error.isEmpty()) error = tr("GitHub returned an invalid workflow jobs response.");
            emit workflowRunJobsLoaded(runId, std::move(jobs), std::move(error));
        });
}

void GitHubService::loadWorkflowHistory(const GitHubRepository& repository, int page)
{
    const QString root = repoPath(repository);
    if (root.isEmpty()) {
        emit workflowHistoryLoaded({}, false, page, tr("The selected GitHub repository is invalid."));
        return;
    }
    const quint64 generation = m_dashboardGeneration;
    const QString path = root + QStringLiteral("/actions/runs?per_page=50&page=%1")
        .arg(qBound(1, page, 1000));
    requestApi(path, [this, generation, page](QJsonDocument document, QString error, int status, QByteArray link) {
        if (generation != m_dashboardGeneration) return;
        if (!error.isEmpty() || status != 200 || !document.isObject()) {
            emit workflowHistoryLoaded({}, false, page, error.isEmpty()
                ? tr("GitHub returned an invalid workflow history response.") : error);
            return;
        }
        QVector<GitHubWorkflowRun> runs;
        for (const auto& value : document.object().value(QStringLiteral("workflow_runs")).toArray()) {
            if (!value.isObject()) continue;
            const auto object = value.toObject();
            GitHubWorkflowRun run;
            run.id = object.value(QStringLiteral("id")).toVariant().toLongLong();
            run.name = boundedText(object.value(QStringLiteral("name")), 256);
            run.status = boundedText(object.value(QStringLiteral("status")), 64);
            run.conclusion = boundedText(object.value(QStringLiteral("conclusion")), 64);
            run.headSha = boundedText(object.value(QStringLiteral("head_sha")), 64);
            run.htmlUrl = boundedText(object.value(QStringLiteral("html_url")), 2048);
            run.startedAtUtc = dateValue(object.value(QStringLiteral("run_started_at")));
            if (!run.startedAtUtc.isValid()) run.startedAtUtc = dateValue(object.value(QStringLiteral("created_at")));
            run.completedAtUtc = dateValue(object.value(QStringLiteral("updated_at")));
            if (run.id > 0 && validGitHubWebUrl(run.htmlUrl)) runs.push_back(std::move(run));
        }
        emit workflowHistoryLoaded(std::move(runs), link.contains("rel=\"next\""), page, {});
    });
}

void GitHubService::loadWorkflowJobLogs(const GitHubRepository& repository, qint64 jobId)
{
    const QString root = repoPath(repository);
    if (root.isEmpty() || jobId <= 0) {
        emit workflowJobLogsLoaded(jobId, {}, false, tr("The selected workflow job is invalid."));
        return;
    }
    const quint64 generation = m_dashboardGeneration;
    ensureValidToken([this, root, jobId, generation](bool ok, QString error) {
        if (generation != m_dashboardGeneration) return;
        if (!ok) { emit workflowJobLogsLoaded(jobId, {}, false, error); return; }
        QNetworkRequest request(QUrl(QString::fromLatin1(ApiRoot)
            + root + QStringLiteral("/actions/jobs/%1/logs").arg(jobId)));
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setRawHeader("X-GitHub-Api-Version", ApiVersion);
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_credentials.accessToken.toUtf8());
        request.setTransferTimeout(RequestTimeoutMs);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto* reply = m_network->get(request);
        m_dashboardReplies.push_back(reply);
        auto body = std::make_shared<QByteArray>();
        auto oversized = std::make_shared<bool>(false);
        connect(reply, &QIODevice::readyRead, this, [reply, body, oversized] {
            body->append(reply->readAll());
            if (body->size() > 64 * 1024) {
                *oversized = true;
                body->clear();
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this,
            [this, reply, body, oversized, generation, jobId]( ) {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QUrl location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                if (location.isEmpty()) location = QUrl::fromEncoded(reply->rawHeader("Location"), QUrl::StrictMode);
                if (!location.isEmpty()) location = reply->url().resolved(location);
                for (auto it = m_dashboardReplies.begin(); it != m_dashboardReplies.end();) {
                    if (!*it || *it == reply) it = m_dashboardReplies.erase(it); else ++it;
                }
                const bool transportError = reply->error() != QNetworkReply::NoError;
                reply->deleteLater();
                if (generation != m_dashboardGeneration) return;
                if (*oversized || (transportError && status == 0)) {
                    emit workflowJobLogsLoaded(jobId, {}, false, tr("DevHub could not safely retrieve the job log."));
                    return;
                }
                if (status != 302 || !trustedLogDownloadUrl(location)) {
                    emit workflowJobLogsLoaded(jobId, {}, false,
                        status == 403 || status == 404
                            ? tr("GitHub denied access to this job log. Check the App's Actions read permission.")
                            : tr("GitHub did not return a trusted job log download."));
                    return;
                }
                QNetworkRequest downloadRequest(location);
                downloadRequest.setRawHeader("Accept", "text/plain");
                downloadRequest.setTransferTimeout(RequestTimeoutMs);
                downloadRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
                auto* download = m_network->get(downloadRequest);
                m_dashboardReplies.push_back(download);
                auto log = std::make_shared<QByteArray>();
                auto truncated = std::make_shared<bool>(false);
                connect(download, &QIODevice::readyRead, this, [download, log, truncated] {
                    log->append(download->readAll());
                    constexpr qsizetype limit = 2 * 1024 * 1024;
                    if (log->size() > limit) {
                        log->truncate(limit);
                        *truncated = true;
                        download->abort();
                    }
                });
                connect(download, &QNetworkReply::finished, this,
                    [this, download, log, truncated, generation, jobId] {
                        const int downloadStatus = download->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                        const bool failed = download->error() != QNetworkReply::NoError && !*truncated;
                        for (auto it = m_dashboardReplies.begin(); it != m_dashboardReplies.end();) {
                            if (!*it || *it == download) it = m_dashboardReplies.erase(it); else ++it;
                        }
                        download->deleteLater();
                        if (generation != m_dashboardGeneration) return;
                        if ((failed || (downloadStatus != 200 && !*truncated)) || log->isEmpty()) {
                            emit workflowJobLogsLoaded(jobId, {}, false, tr("The job log could not be downloaded."));
                            return;
                        }
                        QString text = QString::fromUtf8(*log);
                        constexpr qsizetype displayLimit = 128 * 1024;
                        if (text.size() > displayLimit) {
                            text.truncate(displayLimit);
                            *truncated = true;
                        }
                        emit workflowJobLogsLoaded(jobId, std::move(text), *truncated, {});
                    });
            });
    });
}

void GitHubService::loadMonitorSnapshot(const GitHubRepository& repository)
{
    const QString root = repoPath(repository);
    if (root.isEmpty()) {
        emit monitorSnapshotLoaded({}, tr("The selected GitHub repository is invalid."));
        return;
    }
    const quint64 generation = m_monitorGeneration;
    auto snapshot = std::make_shared<GitHubMonitorSnapshot>();
    auto remaining = std::make_shared<int>(2);
    auto failure = std::make_shared<QString>();
    const auto finishOne = [this, snapshot, remaining, failure, generation](QString error) {
        if (!error.isEmpty() && failure->isEmpty()) *failure = std::move(error);
        --*remaining;
        if (*remaining == 0 && generation == m_monitorGeneration)
            emit monitorSnapshotLoaded(std::move(*snapshot), *failure);
    };
    requestApi(root + QStringLiteral("/pulls?state=all&sort=updated&direction=desc&per_page=50&page=1"),
        [snapshot, finishOne](QJsonDocument document, QString error, int status, QByteArray) mutable {
            if (error.isEmpty() && status == 200 && document.isArray()) {
                for (const auto& value : document.array()) {
                    if (!value.isObject()) continue;
                    const auto object = value.toObject();
                    const auto head = object.value(QStringLiteral("head")).toObject();
                    const auto base = object.value(QStringLiteral("base")).toObject();
                    const auto user = object.value(QStringLiteral("user")).toObject();
                    GitHubPullRequest item;
                    item.number = object.value(QStringLiteral("number")).toInt();
                    item.title = boundedText(object.value(QStringLiteral("title")), 4096);
                    item.author = boundedText(user.value(QStringLiteral("login")), 100);
                    item.headBranch = boundedText(head.value(QStringLiteral("ref")), 256);
                    item.baseBranch = boundedText(base.value(QStringLiteral("ref")), 256);
                    item.state = boundedText(object.value(QStringLiteral("state")), 32);
                    item.draft = object.value(QStringLiteral("draft")).toBool();
                    item.merged = object.value(QStringLiteral("merged_at")).isString();
                    item.updatedAtUtc = dateValue(object.value(QStringLiteral("updated_at")));
                    item.htmlUrl = boundedText(object.value(QStringLiteral("html_url")), 2048);
                    if (item.number > 0 && !item.title.isEmpty() && validGitHubWebUrl(item.htmlUrl))
                        snapshot->pullRequests.push_back(std::move(item));
                }
            } else if (error.isEmpty()) error = QObject::tr("GitHub returned an invalid Pull Request monitoring response.");
            finishOne(std::move(error));
        }, false, 0, true);
    requestApi(root + QStringLiteral("/actions/runs?per_page=50&page=1"),
        [snapshot, finishOne](QJsonDocument document, QString error, int status, QByteArray) mutable {
            if (error.isEmpty() && status == 200 && document.isObject()) {
                for (const auto& value : document.object().value(QStringLiteral("workflow_runs")).toArray()) {
                    if (!value.isObject()) continue;
                    const auto object = value.toObject();
                    GitHubWorkflowRun run;
                    run.id = object.value(QStringLiteral("id")).toVariant().toLongLong();
                    run.name = boundedText(object.value(QStringLiteral("name")), 256);
                    run.status = boundedText(object.value(QStringLiteral("status")), 64);
                    run.conclusion = boundedText(object.value(QStringLiteral("conclusion")), 64);
                    run.headSha = boundedText(object.value(QStringLiteral("head_sha")), 64);
                    run.htmlUrl = boundedText(object.value(QStringLiteral("html_url")), 2048);
                    run.startedAtUtc = dateValue(object.value(QStringLiteral("run_started_at")));
                    if (!run.startedAtUtc.isValid()) run.startedAtUtc = dateValue(object.value(QStringLiteral("created_at")));
                    run.completedAtUtc = dateValue(object.value(QStringLiteral("updated_at")));
                    if (run.id > 0 && validGitHubWebUrl(run.htmlUrl)) snapshot->workflowRuns.push_back(std::move(run));
                }
            } else if (error.isEmpty()) error = QObject::tr("GitHub returned an invalid Actions monitoring response.");
            finishOne(std::move(error));
        }, false, 0, true);
}

void GitHubService::setConnection(GitHubConnection::State state, QString message)
{
    m_connection.state = state;
    m_connection.message = std::move(message);
    m_connection.login = state == GitHubConnection::State::Connected ? m_credentials.login : QString{};
    m_connection.avatarUrl = state == GitHubConnection::State::Connected ? m_credentials.avatarUrl : QString{};
    m_connection.lastVerifiedAtUtc = state == GitHubConnection::State::Connected
        ? m_credentials.lastVerifiedAtUtc : QDateTime{};
    emit connectionChanged(m_connection);
}

void GitHubService::updateConnectionFromCredentials()
{
    if (!m_sessionActive) setConnection(GitHubConnection::State::Locked, tr("Sign in to DevHub to use GitHub."));
    else if (m_hasCredentials && m_credentials.clientId != m_clientId)
        setConnection(GitHubConnection::State::ReauthorizationRequired,
                      tr("The configured GitHub App Client ID changed. Reconnect GitHub to authorize this app."));
    else if (m_hasCredentials)
        setConnection(GitHubConnection::State::Connected,
                      tr("Connected to GitHub as %1.").arg(m_credentials.login));
    else setConnection(hasValidClientId() ? GitHubConnection::State::Disconnected
                                          : GitHubConnection::State::NotConfigured);
}

bool GitHubService::persistCredentials(const GitHubCredentials& credentials, QString& error)
{
    if (!m_credentialStore || m_localAccountId.isEmpty()) {
        error = tr("Secure GitHub credential storage is unavailable for this DevHub account.");
        return false;
    }
    return m_credentialStore->save(m_localAccountId, credentials, error);
}

std::optional<GitHubService::TokenResponse> GitHubService::parseTokenResponse(const QJsonDocument& document,
                                                                               QString& errorCode)
{
    if (!document.isObject()) { errorCode = QStringLiteral("malformed_response"); return std::nullopt; }
    const auto object = document.object();
    const QString error = object.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) { errorCode = error; return std::nullopt; }
    TokenResponse response;
    response.accessToken = object.value(QStringLiteral("access_token")).toString();
    response.refreshToken = object.value(QStringLiteral("refresh_token")).toString();
    auto parseSeconds = [&object](const QString& key) -> std::optional<int> {
        const auto value = object.value(key);
        if (value.isUndefined() || value.isNull()) return std::nullopt;
        bool ok = false;
        const int seconds = value.toVariant().toInt(&ok);
        if (!ok || seconds <= 0 || seconds > 366 * 24 * 60 * 60) return std::nullopt;
        return seconds;
    };
    response.accessExpiresIn = parseSeconds(QStringLiteral("expires_in"));
    response.refreshExpiresIn = parseSeconds(QStringLiteral("refresh_token_expires_in"));
    if (response.accessToken.isEmpty() || response.accessToken.size() > 4096
        || (!response.refreshToken.isEmpty() && response.refreshToken.size() > 4096)) {
        errorCode = QStringLiteral("malformed_response");
        return std::nullopt;
    }
    if (object.contains(QStringLiteral("expires_in")) && !response.accessExpiresIn) {
        errorCode = QStringLiteral("malformed_response");
        return std::nullopt;
    }
    if (object.contains(QStringLiteral("refresh_token_expires_in")) && !response.refreshExpiresIn) {
        errorCode = QStringLiteral("malformed_response");
        return std::nullopt;
    }
    if (response.accessExpiresIn && response.refreshToken.isEmpty()) {
        errorCode = QStringLiteral("malformed_response");
        return std::nullopt;
    }
    return response;
}

bool GitHubService::trustedGitHubUrl(const QString& url)
{
    const QUrl parsed(url, QUrl::StrictMode);
    return parsed.isValid() && parsed.scheme() == QStringLiteral("https")
        && parsed.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0
        && parsed.userInfo().isEmpty() && parsed.port(-1) == -1
        && parsed.path() == QStringLiteral("/login/device")
        && parsed.query().isEmpty() && parsed.fragment().isEmpty();
}

} // namespace devhub::application::github
