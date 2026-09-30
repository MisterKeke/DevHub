#include "AuthenticationWidgets.h"

#include "../../application/authentication/AuthenticationProductPolicy.h"
#include "../icons/IconLoader.h"
#include "../theme/ThemePalette.h"
#include "../theme/ThemeTokens.h"
#include "../widgets/ReusableWidgets.h"

#include <QCheckBox>
#include <QAccessible>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace devhub::presentation::authentication {
namespace {

using devhub::presentation::theme::ColorToken;
using devhub::presentation::theme::ThemePalette;
using devhub::presentation::theme::ThemeTokens;
using devhub::presentation::widgets::AccentButton;
using devhub::presentation::widgets::AppLogoWidget;
using devhub::presentation::widgets::FormField;
using devhub::presentation::widgets::PasswordField;
using devhub::presentation::widgets::SecondaryButton;

QString secondaryTextColor()
{
    return ThemePalette::color(ColorToken::TextSecondary).name();
}

QLabel* makeMessageLabel(QWidget* parent)
{
    auto* message = new QLabel(parent);
    message->setObjectName(QStringLiteral("authenticationMessage"));
    message->setWordWrap(true);
    message->setStyleSheet(QStringLiteral("color: %1;").arg(ThemePalette::color(ColorToken::Error).name()));
    message->hide();
    return message;
}

QWidget* makePasswordEditor(QLineEdit** editor, QWidget* parent)
{
    auto* field = new PasswordField(parent);
    *editor = field->editor();
    return field;
}

} // namespace

BrandHeaderWidget::BrandHeaderWidget(QString title, QString subtitle, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Three));
    auto* logo = new AppLogoWidget(this);
    auto* titleColumn = new QVBoxLayout();
    titleColumn->setSpacing(3);
    auto* titleLabel = new QLabel(std::move(title), this);
    titleLabel->setObjectName(QStringLiteral("authenticationTitle"));
    titleLabel->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::WindowTitle));
    auto* subtitleLabel = new QLabel(std::move(subtitle), this);
    subtitleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(secondaryTextColor()));
    titleColumn->addWidget(titleLabel);
    titleColumn->addWidget(subtitleLabel);
    layout->addWidget(logo);
    layout->addLayout(titleColumn);
    layout->addStretch();
}

CreateAccountWidget::CreateAccountWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Three));
    auto* usernameEditor = new QLineEdit(this);
    usernameEditor->setObjectName(QStringLiteral("createUsername"));
    usernameEditor->setAccessibleName(tr("Username"));
    usernameEditor->setMaxLength(128);
    m_username = usernameEditor;
    auto* passwordField = makePasswordEditor(&m_password, this);
    m_password->setObjectName(QStringLiteral("createPassword"));
    m_password->setAccessibleName(tr("Password"));
    auto* confirmationField = makePasswordEditor(&m_confirmation, this);
    m_confirmation->setObjectName(QStringLiteral("confirmPassword"));
    m_confirmation->setAccessibleName(tr("Confirm password"));
    layout->addWidget(new FormField(tr("Username"), usernameEditor,
                                    tr("Up to 64 characters. Avoid control characters and path separators."), true, this));
    layout->addWidget(new FormField(tr("Password"), passwordField,
                                    tr("Use a password you can remember. Password-manager paste is supported."), true, this));
    layout->addWidget(new FormField(tr("Confirm password"), confirmationField, {}, true, this));
    auto* securityNote = new QLabel(
        tr("Your account is stored locally. DevHub uses your password to control access to this app; it does not encrypt your workspace files, source code, databases, logs, or notes."), this);
    securityNote->setWordWrap(true);
    securityNote->setStyleSheet(QStringLiteral("color: %1;").arg(secondaryTextColor()));
    layout->addWidget(securityNote);
    m_message = makeMessageLabel(this);
    layout->addWidget(m_message);
    m_createButton = new AccentButton(tr("Create Account"), this);
    m_createButton->setObjectName(QStringLiteral("createAccountButton"));
    m_createButton->setDefault(true);
    layout->addWidget(m_createButton);
    connect(m_createButton, &QPushButton::clicked, this, &CreateAccountWidget::createRequested);
    connect(m_confirmation, &QLineEdit::returnPressed, m_createButton, &QPushButton::click);
}

QString CreateAccountWidget::username() const { return m_username->text(); }
QString CreateAccountWidget::password() const { return m_password->text(); }
QString CreateAccountWidget::confirmation() const { return m_confirmation->text(); }

void CreateAccountWidget::clearPasswords()
{
    m_password->clear();
    m_confirmation->clear();
}

void CreateAccountWidget::setBusy(bool busy)
{
    m_createButton->setEnabled(!busy);
    m_username->setEnabled(!busy);
    m_password->setEnabled(!busy);
    m_confirmation->setEnabled(!busy);
    m_createButton->setText(busy ? tr("Creating account…") : tr("Create Account"));
}

void CreateAccountWidget::showMessage(const QString& message)
{
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

void CreateAccountWidget::focusUsername() { m_username->setFocus(Qt::OtherFocusReason); }

SignInWidget::SignInWidget(QString username, QWidget* parent, bool keepSessionDefault)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Three));
    auto* usernameEditor = new QLineEdit(this);
    usernameEditor->setObjectName(QStringLiteral("signInUsername"));
    usernameEditor->setAccessibleName(tr("Username"));
    usernameEditor->setMaxLength(128);
    usernameEditor->setText(std::move(username));
    m_username = usernameEditor;
    auto* passwordField = makePasswordEditor(&m_password, this);
    m_password->setObjectName(QStringLiteral("signInPassword"));
    m_password->setAccessibleName(tr("Password"));
    layout->addWidget(new FormField(tr("Username"), usernameEditor, {}, true, this));
    layout->addWidget(new FormField(tr("Password"), passwordField, {}, true, this));
    m_keepSession = new QCheckBox(tr("Keep me signed in for this session"), this);
    m_keepSession->setChecked(keepSessionDefault);
    m_keepSession->setToolTip(tr("This applies only until you quit. Manual Lock still requires your password."));
    m_keepSession->setAccessibleDescription(m_keepSession->toolTip());
    auto* forgotPassword = new QPushButton(tr("Forgot password?"), this);
    forgotPassword->setFlat(true);
    forgotPassword->setAccessibleName(tr("Forgot password and reset local profile"));
    forgotPassword->setStyleSheet(QStringLiteral("text-align: right; color: %1; border: none; padding: 2px;")
                                      .arg(ThemePalette::color(ColorToken::Accent).name()));
    auto* sessionRow = new QHBoxLayout();
    sessionRow->addWidget(m_keepSession);
    sessionRow->addStretch();
    sessionRow->addWidget(forgotPassword);
    layout->addLayout(sessionRow);
    m_message = makeMessageLabel(this);
    layout->addWidget(m_message);
    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(12);
    m_signInButton = new AccentButton(tr("Unlock DevHub"), this);
    m_signInButton->setObjectName(QStringLiteral("signInButton"));
    m_signInButton->setDefault(true);
    m_signInButton->setMinimumHeight(50);
    actionRow->addWidget(m_signInButton, 7);
    auto* quitButton = new SecondaryButton(tr("Quit"), this);
    quitButton->setMinimumHeight(50);
    actionRow->addWidget(quitButton, 3);
    layout->addLayout(actionRow);
    connect(m_signInButton, &QPushButton::clicked, this, &SignInWidget::signInRequested);
    connect(forgotPassword, &QPushButton::clicked, this, &SignInWidget::forgotPasswordRequested);
    connect(m_password, &QLineEdit::returnPressed, m_signInButton, &QPushButton::click);
    connect(m_username, &QLineEdit::returnPressed, this, [this] { m_password->setFocus(); });
    connect(quitButton, &QPushButton::clicked, this, [this] { window()->close(); });
}

QString SignInWidget::username() const { return m_username->text(); }
QString SignInWidget::password() const { return m_password->text(); }
bool SignInWidget::keepSession() const { return m_keepSession->isChecked(); }
void SignInWidget::clearPassword() { m_password->clear(); }

void SignInWidget::setBusy(bool busy)
{
    m_busy = busy;
    m_username->setEnabled(!busy);
    m_password->setEnabled(!busy);
    m_keepSession->setEnabled(!busy);
    m_signInButton->setEnabled(!busy && m_retryRemainingMs == 0);
    m_signInButton->setText(busy ? tr("Unlocking…") : tr("Unlock DevHub"));
}

void SignInWidget::setRetryRemaining(int milliseconds)
{
    m_retryRemainingMs = qMax(0, milliseconds);
    const bool waiting = m_retryRemainingMs > 0;
    if (waiting) {
        m_signInButton->setEnabled(false);
        m_signInButton->setText(tr("Try again in %1 s").arg((m_retryRemainingMs + 999) / 1000));
    } else if (!m_busy) {
        m_signInButton->setEnabled(true);
        m_signInButton->setText(tr("Unlock DevHub"));
    }
}

void SignInWidget::showMessage(const QString& message)
{
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

void SignInWidget::focusInitialField(bool usernameIsValid)
{
    (usernameIsValid ? m_password : m_username)->setFocus(Qt::OtherFocusReason);
}

UnlockWidget::UnlockWidget(QString username, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Three));
    auto* lockTile = new QLabel(this);
    lockTile->setAlignment(Qt::AlignCenter);
    lockTile->setFixedSize(68, 68);
    lockTile->setPixmap(devhub::presentation::icons::IconLoader::icon(
        devhub::presentation::icons::IconName::Lock,
        ThemePalette::color(ColorToken::Accent), 36).pixmap(36, 36));
    lockTile->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 16px;")
        .arg(ThemePalette::color(ColorToken::RaisedSurface).name(),
             ThemePalette::color(ColorToken::BorderStrong).name()));
    auto* title = new QLabel(tr("Unlock Workspace Hub"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::PageTitle));
    title->setStyleSheet(QStringLiteral("font-size: 22pt; font-weight: 600;"));
    auto* subtitle = new QLabel(tr("Enter your password to return to your workspaces, notes, logs, and database."), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QStringLiteral("color: %1;").arg(secondaryTextColor()));
    m_identity = new QLabel(tr("Signed in as %1").arg(std::move(username)), this);
    m_identity->setObjectName(QStringLiteral("lockedAccountIdentity"));
    m_identity->setAccessibleName(tr("Signed-in account"));
    m_identity->setAlignment(Qt::AlignCenter);
    m_identity->setStyleSheet(QStringLiteral("color: %1; padding: 4px;").arg(secondaryTextColor()));
    auto* passwordField = makePasswordEditor(&m_password, this);
    m_password->setObjectName(QStringLiteral("unlockPassword"));
    m_password->setAccessibleName(tr("Password"));
    layout->addWidget(lockTile, 0, Qt::AlignHCenter);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(m_identity);
    layout->addWidget(new FormField(tr("Password"), passwordField, {}, true, this));
    m_message = makeMessageLabel(this);
    layout->addWidget(m_message);
    m_unlockButton = new AccentButton(tr("Unlock DevHub"), this);
    m_unlockButton->setObjectName(QStringLiteral("unlockButton"));
    m_unlockButton->setDefault(true);
    m_unlockButton->setMinimumHeight(50);
    layout->addWidget(m_unlockButton);
    auto* actions = new QHBoxLayout();
    auto* signOutButton = new SecondaryButton(tr("Sign Out"), this);
    auto* quitButton = new SecondaryButton(tr("Quit"), this);
    actions->addWidget(signOutButton);
    actions->addWidget(quitButton);
    layout->addLayout(actions);
    connect(m_unlockButton, &QPushButton::clicked, this, &UnlockWidget::unlockRequested);
    connect(m_password, &QLineEdit::returnPressed, m_unlockButton, &QPushButton::click);
    connect(signOutButton, &QPushButton::clicked, this, &UnlockWidget::signOutRequested);
    connect(quitButton, &QPushButton::clicked, this, &UnlockWidget::quitRequested);
}

QString UnlockWidget::password() const { return m_password->text(); }
void UnlockWidget::setUsername(QString username)
{
    m_identity->setText(tr("Signed in as %1").arg(std::move(username)));
}
void UnlockWidget::clearPassword() { m_password->clear(); }

void UnlockWidget::setBusy(bool busy)
{
    m_busy = busy;
    m_password->setEnabled(!busy);
    m_unlockButton->setEnabled(!busy && m_retryRemainingMs == 0);
        m_unlockButton->setText(busy ? tr("Unlocking…") : tr("Unlock DevHub"));
}

void UnlockWidget::setRetryRemaining(int milliseconds)
{
    m_retryRemainingMs = qMax(0, milliseconds);
    if (m_retryRemainingMs > 0) {
        m_unlockButton->setEnabled(false);
        m_unlockButton->setText(tr("Try again in %1 s").arg((m_retryRemainingMs + 999) / 1000));
    } else if (!m_busy) {
        m_unlockButton->setEnabled(true);
        m_unlockButton->setText(tr("Unlock DevHub"));
    }
}

void UnlockWidget::showMessage(const QString& message)
{
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

void UnlockWidget::focusPassword() { m_password->setFocus(Qt::OtherFocusReason); }

} // namespace devhub::presentation::authentication
