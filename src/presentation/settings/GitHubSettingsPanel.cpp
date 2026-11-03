#include "GitHubSettingsPanel.h"
#include "GitHubDeviceFlowDialog.h"

#include "../../application/github/GitHubService.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace devhub::presentation::settings {

GitHubSettingsPanel::GitHubSettingsPanel(application::github::GitHubService& service, QWidget* parent)
    : QWidget(parent), m_service(service), m_avatarNetwork()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto* row = new QHBoxLayout;
    m_avatar = new QLabel(this);
    m_avatar->setFixedSize(48, 48);
    m_avatar->setAlignment(Qt::AlignCenter);
    m_avatar->setText(QStringLiteral("GH"));
    row->addWidget(m_avatar, 0, Qt::AlignTop);
    auto* info = new QVBoxLayout;
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("githubConnectionStatus"));
    m_status->setStyleSheet(QStringLiteral("font-weight: 600;"));
    m_status->setTextFormat(Qt::PlainText);
    m_account = new QLabel(this);
    m_account->setObjectName(QStringLiteral("githubConnectedAccount"));
    info->addWidget(m_status);
    info->addWidget(m_account);
    row->addLayout(info, 1);
    layout->addLayout(row);

    m_authMethod = new QLabel(tr("Authentication method: OAuth Device Flow"), this);
    m_tokenStatus = new QLabel(this);
    m_lastVerified = new QLabel(this);
    m_appStatus = new QLabel(this);
    m_appStatus->setObjectName(QStringLiteral("githubAppConfigurationStatus"));
    for (auto* label : {m_authMethod, m_tokenStatus, m_lastVerified, m_appStatus}) {
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    auto* buttons = new QHBoxLayout;
    m_connect = new QPushButton(tr("Connect GitHub"), this);
    m_connect->setObjectName(QStringLiteral("connectGitHubButton"));
    m_recheck = new QPushButton(tr("Recheck Connection"), this);
    m_recheck->setObjectName(QStringLiteral("recheckGitHubButton"));
    m_disconnect = new QPushButton(tr("Disconnect GitHub"), this);
    m_disconnect->setObjectName(QStringLiteral("disconnectGitHubButton"));
    buttons->addWidget(m_connect);
    buttons->addWidget(m_recheck);
    buttons->addWidget(m_disconnect);
    layout->addLayout(buttons);
    auto* revoke = new QPushButton(tr("Manage or revoke authorization on GitHub…"), this);
    revoke->setFlat(true);
    revoke->setObjectName(QStringLiteral("revokeGitHubAuthorizationButton"));
    layout->addWidget(revoke, 0, Qt::AlignLeft);
    layout->addStretch();

    connect(m_connect, &QPushButton::clicked, this, [this] {
        GitHubDeviceFlowDialog dialog(m_service, this);
        dialog.exec();
    });
    connect(m_recheck, &QPushButton::clicked, &m_service, &application::github::GitHubService::recheckConnection);
    connect(m_disconnect, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Disconnect GitHub"),
                tr("Remove the GitHub credentials saved for this DevHub account? This does not revoke the authorization on GitHub."))
            != QMessageBox::Yes) return;
        m_service.disconnectGitHub();
    });
    connect(revoke, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/settings/apps/authorizations")));
    });
    connect(&m_service, &application::github::GitHubService::connectionChanged,
            this, &GitHubSettingsPanel::updateState);
    updateState(m_service.connection());
}

void GitHubSettingsPanel::updateState(const application::github::GitHubConnection& connection)
{
    using State = application::github::GitHubConnection::State;
    m_status->setText(connection.message.isEmpty() ? tr("GitHub is not connected.") : connection.message);
    m_account->setText(connection.state == State::Connected
        ? tr("Connected account: %1").arg(connection.login) : tr("No connected account"));
    m_tokenStatus->setText(connection.state == State::Connected ? tr("Token/session status: valid")
        : connection.state == State::Verifying ? tr("Token/session status: verifying")
        : connection.state == State::ReauthorizationRequired ? tr("Token/session status: reauthorization required")
        : tr("Token/session status: not connected"));
    m_lastVerified->setText(connection.lastVerifiedAtUtc.isValid()
        ? tr("Last successful verification: %1").arg(QLocale().toString(connection.lastVerifiedAtUtc.toLocalTime(), QLocale::ShortFormat))
        : tr("Last successful verification: not available"));
    m_appStatus->setText(m_service.hasValidClientId()
        ? tr("GitHub App configuration: OAuth Client ID configured")
        : tr("GitHub App configuration: Client ID required. Enter the OAuth Client ID below; the GitHub App ID is a different value."));
    const bool active = connection.state == State::Connecting || connection.state == State::WaitingForAuthorization
        || connection.state == State::Verifying || connection.state == State::Locked;
    m_connect->setEnabled(m_service.hasValidClientId() && !active);
    m_connect->setText(connection.state == State::Connected || connection.state == State::ReauthorizationRequired
        ? tr("Reconnect GitHub") : tr("Connect GitHub"));
    m_recheck->setEnabled(m_service.hasValidClientId() && connection.state != State::Locked && !active);
    m_disconnect->setEnabled(connection.state == State::Connected || connection.state == State::ReauthorizationRequired
        || connection.state == State::Error);
    if (connection.state == State::Connected) loadAvatar(connection.avatarUrl);
    else { m_avatarUrl.clear(); m_avatar->setPixmap({}); m_avatar->setText(QStringLiteral("GH")); }
}

void GitHubSettingsPanel::loadAvatar(const QString& value)
{
    if (m_avatarUrl == value) return;
    m_avatarUrl = value;
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("https")
        || url.host() != QStringLiteral("avatars.githubusercontent.com") || !url.userInfo().isEmpty()
        || (url.port(-1) != -1 && url.port() != 443)) return;
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    auto* reply = m_avatarNetwork.get(request);
    auto bytes = std::make_shared<QByteArray>();
    auto oversized = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, bytes, oversized] {
        bytes->append(reply->readAll());
        if (bytes->size() > 1024 * 1024) {
            *oversized = true;
            bytes->clear();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, oversized, value] {
        if (reply->error() == QNetworkReply::NoError && !*oversized) {
            QPixmap avatar;
            if (m_avatarUrl == value
                && m_service.connection().state == application::github::GitHubConnection::State::Connected
                && m_service.connection().avatarUrl == value && avatar.loadFromData(*bytes))
                m_avatar->setPixmap(avatar.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        reply->deleteLater();
    });
}

} // namespace devhub::presentation::settings
