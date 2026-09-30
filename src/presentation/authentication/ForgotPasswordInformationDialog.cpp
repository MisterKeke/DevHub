#include "ForgotPasswordInformationDialog.h"

#include "../theme/ThemePalette.h"
#include "../theme/ThemeTokens.h"
#include "../widgets/ReusableWidgets.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace devhub::presentation::authentication {
using devhub::presentation::theme::ColorToken;
using devhub::presentation::theme::ThemePalette;

ForgotPasswordInformationDialog::ForgotPasswordInformationDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Reset local profile"));
    setModal(true);
    setMinimumWidth(480);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        tr("DevHub has no email recovery. Resetting creates a new local profile and moves the old DevHub database and account folders into a timestamped recovery folder. Workspace folders, source files, and other external data are left where they are. The old profile will not be imported automatically."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* confirmationLabel = new QLabel(tr("Type RESET to confirm this profile reset."), this);
    layout->addWidget(confirmationLabel);
    m_confirmation = new QLineEdit(this);
    m_confirmation->setAccessibleName(tr("Type RESET to confirm"));
    m_confirmation->setMaxLength(8);
    layout->addWidget(m_confirmation);
    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    m_message->setStyleSheet(QStringLiteral("color: %1;")
                                 .arg(ThemePalette::color(ColorToken::Error).name()));
    m_message->hide();
    layout->addWidget(m_message);
    m_resetButton = new devhub::presentation::widgets::DestructiveButton(tr("Reset Profile"), this);
    m_resetButton->setEnabled(false);
    layout->addWidget(m_resetButton);
    auto* cancelButton = new devhub::presentation::widgets::SecondaryButton(tr("Cancel"), this);
    layout->addWidget(cancelButton);
    connect(m_confirmation, &QLineEdit::textChanged, this, [this](const QString& value) {
        m_resetButton->setEnabled(value == QStringLiteral("RESET"));
    });
    connect(m_resetButton, &QPushButton::clicked, this, &ForgotPasswordInformationDialog::resetRequested);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

QString ForgotPasswordInformationDialog::confirmationText() const { return m_confirmation->text(); }

void ForgotPasswordInformationDialog::setBusy(bool busy)
{
    m_confirmation->setEnabled(!busy);
    m_resetButton->setEnabled(!busy && m_confirmation->text() == QStringLiteral("RESET"));
    m_resetButton->setText(busy ? tr("Moving profile to recovery…") : tr("Reset Profile"));
}

void ForgotPasswordInformationDialog::showMessage(const QString& message)
{
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

} // namespace devhub::presentation::authentication
