#include "RecoveryModeWindow.h"

#include "../presentation/errors/ErrorPresenter.h"
#include "../shared/security/RedactionService.h"

#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QUuid>

namespace devhub::app {

RecoveryModeWindow::RecoveryModeWindow(StartupFailure failure, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("DevHub startup recovery"));
    setModal(true);
    setMinimumWidth(520);

    shared::security::RedactionService redactor;
    presentation::errors::ErrorPresenter presenter(redactor);
    const auto presented = presenter.present(failure.error);

    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(QStringLiteral("DevHub could not finish starting"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* message = new QLabel(presented.userMessage, this);
    message->setWordWrap(true);
    layout->addWidget(message);

    auto* details = new QPlainTextEdit(presented.technicalDetails, this);
    details->setReadOnly(true);
    details->setVisible(false);
    details->setMaximumHeight(180);
    auto* detailsButton = new QPushButton(QStringLiteral("Show technical details"), this);
    connect(detailsButton, &QPushButton::clicked, this, [details, detailsButton] {
        const bool showDetails = !details->isVisible();
        details->setVisible(showDetails);
        detailsButton->setText(showDetails ? QStringLiteral("Hide technical details")
                                           : QStringLiteral("Show technical details"));
    });
    layout->addWidget(detailsButton);
    layout->addWidget(details);

    if (!failure.recoverableFilePath.isEmpty() && !failure.recoveryCopyDirectory.isEmpty()) {
        auto* copyMessage = new QLabel(this);
        copyMessage->setWordWrap(true);
        auto* copyButton = new QPushButton(QStringLiteral("Copy database aside"), this);
        connect(copyButton, &QPushButton::clicked, this,
                [path = failure.recoverableFilePath,
                 destinationDirectory = failure.recoveryCopyDirectory,
                 copyButton,
                 copyMessage] {
            if (!QFileInfo::exists(path)) {
                copyMessage->setText(QStringLiteral("The database file is not present to copy."));
                return;
            }
            if (!QDir().mkpath(destinationDirectory)) {
                copyMessage->setText(QStringLiteral("DevHub could not create the recovery-copy folder."));
                return;
            }
            const QString fileName = QStringLiteral("%1.recovery-copy-%2-%3.bak")
                                         .arg(QFileInfo(path).fileName(),
                                              QDateTime::currentDateTimeUtc().toString(
                                                  QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'")),
                                              QUuid::createUuid().toString(QUuid::WithoutBraces));
            const QString destination = QDir(destinationDirectory).filePath(fileName);
            QFile source(path);
            if (source.copy(destination)) {
                copyMessage->setText(QStringLiteral("Recovery copy saved to: %1").arg(destination));
                copyButton->setEnabled(false);
            } else {
                copyMessage->setText(QStringLiteral("DevHub could not copy the database file: %1")
                                         .arg(source.errorString()));
            }
        });
        layout->addWidget(copyButton);
        layout->addWidget(copyMessage);
    }

    auto* buttons = new QDialogButtonBox(this);
    auto* retryButton = buttons->addButton(QStringLiteral("Retry"), QDialogButtonBox::AcceptRole);
    auto* quitButton = buttons->addButton(QStringLiteral("Quit"), QDialogButtonBox::RejectRole);
    connect(retryButton, &QPushButton::clicked, this, [this] {
        m_selectedAction = StartupRecoveryAction::Retry;
        accept();
    });
    connect(quitButton, &QPushButton::clicked, this, [this] {
        m_selectedAction = StartupRecoveryAction::Quit;
        reject();
    });
    layout->addWidget(buttons);
}

} // namespace devhub::app
