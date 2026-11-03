#include "GitHubDeviceFlowDialog.h"

#include "../../application/github/GitHubService.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace devhub::presentation::settings {

GitHubDeviceFlowDialog::GitHubDeviceFlowDialog(application::github::GitHubService& service, QWidget* parent)
    : QDialog(parent), m_service(service)
{
    setWindowTitle(tr("Connect GitHub"));
    setModal(true);
    setMinimumWidth(480);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 22);
    layout->setSpacing(14);
    auto* title = new QLabel(tr("Connect GitHub"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16pt; font-weight: 600;"));
    layout->addWidget(title);
    auto* instructions = new QLabel(tr("To connect your GitHub account, enter the following code on GitHub."), this);
    instructions->setWordWrap(true);
    layout->addWidget(instructions);
    m_code = new QLabel(tr("Requesting a code…"), this);
    m_code->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_code->setAlignment(Qt::AlignCenter);
    m_code->setMinimumHeight(70);
    m_code->setStyleSheet(QStringLiteral("font-size: 24pt; font-weight: 700; letter-spacing: 4px; padding: 12px;"));
    layout->addWidget(m_code);
    m_url = new QLabel(this);
    m_url->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_url->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_url);
    m_countdown = new QLabel(tr("Authorization time remaining: —"), this);
    m_countdown->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_countdown);
    m_state = new QLabel(tr("Waiting for GitHub to prepare the authorization code…"), this);
    m_state->setWordWrap(true);
    m_state->setAlignment(Qt::AlignCenter);
    m_state->setTextFormat(Qt::PlainText);
    layout->addWidget(m_state);

    auto* actions = new QHBoxLayout;
    auto* copy = new QPushButton(tr("Copy Code"), this);
    copy->setEnabled(false);
    m_open = new QPushButton(tr("Open GitHub"), this);
    m_open->setEnabled(false);
    m_finish = new QPushButton(tr("Cancel"), this);
    actions->addWidget(copy);
    actions->addWidget(m_open);
    actions->addStretch();
    actions->addWidget(m_finish);
    layout->addLayout(actions);

    connect(copy, &QPushButton::clicked, this, [this, copy] {
        if (m_code->text().isEmpty() || m_code->text().contains(QChar(0x2026))) return;
        QApplication::clipboard()->setText(m_code->text());
        copy->setText(tr("Copied"));
        QTimer::singleShot(1500, copy, [copy] { copy->setText(QObject::tr("Copy Code")); });
    });
    connect(m_open, &QPushButton::clicked, this, [this] {
        if (m_verificationUri.scheme() == QStringLiteral("https")
            && m_verificationUri.host() == QStringLiteral("github.com")
            && m_verificationUri.path() == QStringLiteral("/login/device"))
            QDesktopServices::openUrl(m_verificationUri);
    });
    connect(m_finish, &QPushButton::clicked, this, [this] {
        if (m_finish->text() == tr("Cancel")) m_service.cancelDeviceFlow();
        accept();
    });
    connect(this, &QDialog::rejected, this, [this] { m_service.cancelDeviceFlow(); });
    connect(&m_service, &application::github::GitHubService::deviceCodeReady,
            this, [this, copy](const QString& code, const QUrl& url, int expires) {
                setAuthorizationCode(code, url, expires);
                copy->setEnabled(true);
                m_open->setEnabled(true);
            });
    connect(&m_service, &application::github::GitHubService::authorizationTimeRemaining,
            this, [this](int remaining) {
                m_countdown->setText(tr("Authorization time remaining: %1:%2")
                    .arg(remaining / 60, 2, 10, QLatin1Char('0'))
                    .arg(remaining % 60, 2, 10, QLatin1Char('0')));
            });
    connect(&m_service, &application::github::GitHubService::connectionChanged,
            this, [this](const application::github::GitHubConnection& state) {
                if (state.state == application::github::GitHubConnection::State::WaitingForAuthorization
                    || state.state == application::github::GitHubConnection::State::Connecting)
                    m_state->setText(state.message);
            });
    connect(&m_service, &application::github::GitHubService::deviceFlowFinished,
            this, [this](bool succeeded, const QString& message) {
                m_state->setText(message);
                m_finish->setText(succeeded ? tr("Done") : tr("Close"));
                if (succeeded) m_state->setStyleSheet(QStringLiteral("font-weight: 600;"));
            });
    m_service.beginDeviceFlow();
}

void GitHubDeviceFlowDialog::setAuthorizationCode(const QString& userCode, const QUrl& verificationUri,
                                                   int expiresInSeconds)
{
    Q_UNUSED(expiresInSeconds);
    m_code->setText(userCode);
    m_verificationUri = verificationUri;
    m_url->setText(verificationUri.toString());
    m_url->setStyleSheet(QStringLiteral("color: #db75a4; text-decoration: underline;"));
}

} // namespace devhub::presentation::settings
