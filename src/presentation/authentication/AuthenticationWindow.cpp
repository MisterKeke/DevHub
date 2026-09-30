#include "AuthenticationWindow.h"

#include "ForgotPasswordInformationDialog.h"
#include "AuthenticationWidgets.h"
#include "../../application/authentication/AuthenticationService.h"
#include "../../application/authentication/AuthenticationValidation.h"
#include "../../application/authentication/SessionController.h"
#include "../icons/IconLoader.h"
#include "../theme/ThemePalette.h"
#include "../theme/ThemeTokens.h"
#include "../widgets/ReusableWidgets.h"

#include <QIcon>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QDebug>
#include <QPointer>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace devhub::presentation::authentication {
using devhub::presentation::theme::ThemeTokens;

AuthenticationWindow::AuthenticationWindow(bool accountExists,
                                           QString storedUsername,
                                           application::authentication::AuthenticationService& authentication,
                                           application::authentication::SessionController& session,
                                           QWidget* parent,
                                           QElapsedTimer* startupTimer,
                                           bool keepSessionDefault)
    : QDialog(parent),
      m_authentication(authentication),
      m_session(session),
      m_startupTimer(startupTimer)
{
    setWindowTitle(accountExists ? tr("Welcome back — DevHub") : tr("Create DevHub Account"));
    setWindowIcon(QIcon(QStringLiteral(":/branding/devhub.svg")));
    setModal(true);
    setMinimumSize(900, 680);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* background = new devhub::presentation::widgets::AuthenticationBackgroundWidget(this);
    background->setObjectName(QStringLiteral("authenticationBackground"));
    auto* backgroundLayout = new QVBoxLayout(background);
    backgroundLayout->setContentsMargins(34, 24, 34, 28);
    backgroundLayout->setSpacing(0);
    auto* header = new QWidget(background);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto* logo = new devhub::presentation::widgets::AppLogoWidget(header);
    auto* brand = new QLabel(QStringLiteral("DevHub"), header);
    brand->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::WindowTitle));
    headerLayout->addWidget(logo);
    headerLayout->addWidget(brand);
    headerLayout->addStretch();
    auto* tagline = new QLabel(tr("A better developer workflow, locally."), header);
    tagline->setStyleSheet(QStringLiteral("color: %1;").arg(
        devhub::presentation::theme::ThemePalette::color(
            devhub::presentation::theme::ColorToken::TextSecondary).name()));
    headerLayout->addWidget(tagline, 0, Qt::AlignVCenter);
    backgroundLayout->addWidget(header);
    backgroundLayout->addStretch(1);

    auto* card = new devhub::presentation::widgets::CardWidget(background);
    card->setObjectName(QStringLiteral("authenticationCard"));
    card->setMaximumWidth(650);
    card->setStyleSheet(QStringLiteral(
        "QFrame#authenticationCard { background: %1;"
        " border: 1px solid %2; border-radius: 18px; }")
        .arg(devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::PanelSurface).name(),
             devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::BorderStrong).name()));
    auto* cardLayout = card->contentLayout();
    cardLayout->setContentsMargins(38, 30, 38, 26);
    cardLayout->setSpacing(12);
    auto* lockTile = new QLabel(card);
    lockTile->setObjectName(QStringLiteral("authenticationLockTile"));
    lockTile->setAlignment(Qt::AlignCenter);
    lockTile->setFixedSize(72, 72);
    lockTile->setPixmap(devhub::presentation::icons::IconLoader::icon(
        devhub::presentation::icons::IconName::Lock,
        devhub::presentation::theme::ThemePalette::color(
            devhub::presentation::theme::ColorToken::Accent), 38).pixmap(38, 38));
    lockTile->setStyleSheet(QStringLiteral(
        "QLabel#authenticationLockTile { background: %1; border: 1px solid %2; border-radius: 16px; }")
        .arg(devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::RaisedSurface).name(),
             devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::BorderStrong).name()));
    cardLayout->addWidget(lockTile, 0, Qt::AlignHCenter);
    auto* cardTitle = new QLabel(accountExists ? tr("Unlock Workspace Hub") : tr("Create your local account"), card);
    cardTitle->setObjectName(QStringLiteral("authenticationTitle"));
    cardTitle->setAlignment(Qt::AlignCenter);
    cardTitle->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::PageTitle));
    cardTitle->setStyleSheet(QStringLiteral("font-size: 24pt; font-weight: 600;"));
    cardLayout->addWidget(cardTitle);
    auto* cardSubtitle = new QLabel(accountExists
        ? tr("Enter your password to access your local workspaces, notes, logs, and database.")
        : tr("Create an account to protect access to your local developer workspace."), card);
    cardSubtitle->setAlignment(Qt::AlignCenter);
    cardSubtitle->setWordWrap(true);
    cardSubtitle->setMaximumWidth(500);
    cardSubtitle->setStyleSheet(QStringLiteral("color: %1;").arg(
        devhub::presentation::theme::ThemePalette::color(
            devhub::presentation::theme::ColorToken::TextSecondary).name()));
    cardLayout->addWidget(cardSubtitle, 0, Qt::AlignHCenter);
    auto* cardGap = new QWidget(card);
    cardGap->setFixedHeight(4);
    cardLayout->addWidget(cardGap);
    m_pages = new QStackedWidget(card);
    m_signIn = new SignInWidget(std::move(storedUsername), m_pages, keepSessionDefault);
    m_createAccount = new CreateAccountWidget(m_pages);
    m_pages->addWidget(m_signIn);
    m_pages->addWidget(m_createAccount);
    m_pages->setCurrentWidget(accountExists ? static_cast<QWidget*>(m_signIn)
                                           : static_cast<QWidget*>(m_createAccount));
    cardLayout->addWidget(m_pages);
    auto* divider = new QFrame(card);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("color: %1;").arg(
        devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::BorderSubtle).name()));
    cardLayout->addWidget(divider);
    auto* localNote = new QLabel(tr("Local-only access protection for your developer workspace."), card);
    localNote->setAlignment(Qt::AlignCenter);
    localNote->setStyleSheet(QStringLiteral("color: %1;").arg(
        devhub::presentation::theme::ThemePalette::color(
            devhub::presentation::theme::ColorToken::TextSecondary).name()));
    cardLayout->addWidget(localNote);
    backgroundLayout->addWidget(card, 0, Qt::AlignHCenter);
    backgroundLayout->addStretch(1);
    root->addWidget(background);

    connect(m_createAccount, &CreateAccountWidget::createRequested,
            this, &AuthenticationWindow::submitCreateAccount);
    if (!accountExists) {
        QTimer::singleShot(0, m_createAccount, &CreateAccountWidget::focusUsername);
    }
    connect(m_signIn, &SignInWidget::signInRequested,
            this, &AuthenticationWindow::submitSignIn);
    connect(m_signIn, &SignInWidget::forgotPasswordRequested,
            this, &AuthenticationWindow::showForgotPasswordInformation);
    const bool usernameValid = domain::account::NormalizedUsername::create(m_signIn->username()).hasValue();
    if (accountExists) {
        QTimer::singleShot(0, this, [this, usernameValid] { m_signIn->focusInitialField(usernameValid); });
    }
    connect(&m_authentication, &application::authentication::AuthenticationService::busyChanged,
            m_signIn, &SignInWidget::setBusy);
    connect(&m_authentication, &application::authentication::AuthenticationService::retryDelayChanged,
            m_signIn, &SignInWidget::setRetryRemaining);
    resize(accountExists ? QSize(1160, 820) : QSize(1160, 900));
}

void AuthenticationWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_startupTimer != nullptr && !m_startupTimeReported) {
        m_startupTimeReported = true;
        qInfo().noquote() << QStringLiteral("DevHub startup to authentication screen: %1 ms")
                                 .arg(m_startupTimer->elapsed());
    }
}

void AuthenticationWindow::showForgotPasswordInformation()
{
    ForgotPasswordInformationDialog dialog(this);
    connect(&dialog, &ForgotPasswordInformationDialog::resetRequested, &dialog, [this, &dialog] {
        dialog.setBusy(true);
        const QString confirmation = dialog.confirmationText();
        QPointer<AuthenticationWindow> window(this);
        QPointer<ForgotPasswordInformationDialog> resetDialog(&dialog);
        m_authentication.resetProfile(confirmation,
            [window, resetDialog](shared::common::Result<void> result) mutable {
                if (!window || !resetDialog) {
                    return;
                }
                resetDialog->setBusy(false);
                if (!result) {
                    resetDialog->showMessage(result.error().userMessage);
                    return;
                }
                window->m_session.profileReset();
                window->m_pages->setCurrentWidget(window->m_createAccount);
                window->setWindowTitle(tr("Create DevHub Account"));
                if (auto* title = window->findChild<QLabel*>(QStringLiteral("authenticationTitle"))) {
                    title->setText(tr("Create DevHub Account"));
                }
                resetDialog->accept();
            });
    });
    dialog.exec();
}

void AuthenticationWindow::submitCreateAccount()
{
    if (m_createAccount == nullptr) {
        return;
    }
    const QString username = m_createAccount->username();
    QString password = m_createAccount->password();
    QString confirmation = m_createAccount->confirmation();
    m_createAccount->clearPasswords();
    m_createAccount->showMessage({});
    m_createAccount->setBusy(true);
    m_session.beginAccountCreation();
    QPointer<AuthenticationWindow> window(this);
    m_authentication.createAccount(username, std::move(password), std::move(confirmation),
        [window](shared::common::Result<domain::account::Account> result) mutable {
            if (!window) {
                return;
            }
            window->m_createAccount->setBusy(false);
            if (!result) {
                window->m_session.accountCreationFailed();
                window->m_createAccount->showMessage(result.error().userMessage);
                return;
            }
            window->m_session.accountCreated(std::move(result).value());
            window->accept();
        });
}

void AuthenticationWindow::submitSignIn()
{
    if (m_signIn == nullptr) {
        return;
    }
    const QString username = m_signIn->username();
    QString password = m_signIn->password();
    const bool keepSession = m_signIn->keepSession();
    m_signIn->clearPassword();
    m_signIn->showMessage({});
    m_signIn->setBusy(true);
    m_session.beginSignIn();
    QPointer<AuthenticationWindow> window(this);
    m_authentication.signIn(username, std::move(password),
        [window, keepSession](shared::common::Result<domain::account::Account> result) mutable {
            if (!window) {
                return;
            }
            window->m_signIn->setBusy(false);
            if (!result) {
                window->m_session.signInFailed();
                window->m_signIn->showMessage(result.error().userMessage);
                return;
            }
            window->m_session.signInSucceeded(std::move(result).value(), keepSession);
            window->accept();
        });
}

} // namespace devhub::presentation::authentication
