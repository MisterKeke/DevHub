#include "AuthenticationWidgets.h"

#include "../icons/IconLoader.h"
#include "../theme/ThemePalette.h"
#include "../theme/ThemeTokens.h"
#include "../widgets/ReusableWidgets.h"

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
    auto* field = new devhub::presentation::widgets::PasswordField(parent);
    *editor = field->editor();
    return field;
}

} // namespace

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
    layout->addWidget(new devhub::presentation::widgets::FormField(tr("Password"), passwordField, {}, true, this));
    m_message = makeMessageLabel(this);
    layout->addWidget(m_message);
    m_unlockButton = new devhub::presentation::widgets::AccentButton(tr("Unlock DevHub"), this);
    m_unlockButton->setObjectName(QStringLiteral("unlockButton"));
    m_unlockButton->setDefault(true);
    m_unlockButton->setMinimumHeight(50);
    layout->addWidget(m_unlockButton);
    auto* actions = new QHBoxLayout();
    auto* signOutButton = new devhub::presentation::widgets::SecondaryButton(tr("Sign Out"), this);
    auto* quitButton = new devhub::presentation::widgets::SecondaryButton(tr("Quit"), this);
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
