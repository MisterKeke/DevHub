#include "ChangePasswordDialog.h"

#include "AuthenticationWidgets.h"
#include "../../application/authentication/AuthenticationService.h"
#include "../../domain/account/Username.h"
#include "../theme/ThemePalette.h"
#include "../theme/ThemeTokens.h"
#include "../widgets/ReusableWidgets.h"

#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace devhub::presentation::authentication {
using devhub::presentation::theme::ColorToken;
using devhub::presentation::theme::ThemePalette;
using devhub::presentation::theme::ThemeTokens;

ChangePasswordDialog::ChangePasswordDialog(
    const domain::account::NormalizedUsername& username,
    application::authentication::AuthenticationService& service,
    QWidget* parent)
    : QDialog(parent),
      m_username(username),
      m_service(service)
{
    setWindowTitle(tr("Change Password"));
    setModal(true);
    setMinimumWidth(500);
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Three));
    auto addPasswordField = [this, layout](const QString& label, QLineEdit** editor) {
        auto* field = new devhub::presentation::widgets::PasswordField(this);
        *editor = field->editor();
        (*editor)->setAccessibleName(label);
        layout->addWidget(new devhub::presentation::widgets::FormField(label, field, {}, true, this));
    };
    addPasswordField(tr("Current password"), &m_currentPassword);
    addPasswordField(tr("New password"), &m_newPassword);
    addPasswordField(tr("Confirm new password"), &m_confirmation);
    auto* guidance = new QLabel(tr("Choose a password you can remember. DevHub does not impose arbitrary character-composition rules."), this);
    guidance->setWordWrap(true);
    guidance->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(ThemePalette::color(ColorToken::TextSecondary).name()));
    layout->addWidget(guidance);
    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    m_message->setStyleSheet(QStringLiteral("color: %1;")
                                 .arg(ThemePalette::color(ColorToken::Error).name()));
    m_message->hide();
    layout->addWidget(m_message);
    m_saveButton = new devhub::presentation::widgets::AccentButton(tr("Update Password"), this);
    layout->addWidget(m_saveButton);
    auto* cancel = new devhub::presentation::widgets::SecondaryButton(tr("Cancel"), this);
    layout->addWidget(cancel);
    connect(m_saveButton, &QPushButton::clicked, this, &ChangePasswordDialog::submit);
    connect(m_confirmation, &QLineEdit::returnPressed, m_saveButton, &QPushButton::click);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_currentPassword->setFocus(Qt::OtherFocusReason);
}

void ChangePasswordDialog::submit()
{
    QString currentPassword = m_currentPassword->text();
    QString newPassword = m_newPassword->text();
    QString confirmation = m_confirmation->text();
    m_currentPassword->clear();
    m_newPassword->clear();
    m_confirmation->clear();
    m_message->clear();
    m_message->hide();
    m_saveButton->setEnabled(false);
    m_saveButton->setText(tr("Verifying and updating…"));
    QPointer<ChangePasswordDialog> dialog(this);
    m_service.changePassword(m_username, std::move(currentPassword), std::move(newPassword),
                             std::move(confirmation),
        [dialog](shared::common::Result<void> result) {
            if (!dialog) {
                return;
            }
            dialog->m_saveButton->setEnabled(true);
            dialog->m_saveButton->setText(tr("Update Password"));
            if (!result) {
                dialog->m_message->setText(result.error().userMessage);
                dialog->m_message->show();
                return;
            }
            dialog->accept();
        });
}

} // namespace devhub::presentation::authentication
