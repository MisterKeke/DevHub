#include "SupportBundleDialog.h"
#include "../../application/support/SupportBundleBuilder.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace devhub::presentation::settings {
SupportBundleDialog::SupportBundleDialog(application::support::SupportBundleBuilder& builder,
    QStringList moduleStatus, QWidget* parent)
    : QDialog(parent), m_builder(builder), m_moduleStatus(std::move(moduleStatus))
{
    setWindowTitle(tr("Create Support Bundle"));
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Review the bundle contents before saving. Private workspace files, databases, and notes are excluded."), this);
    intro->setWordWrap(true); root->addWidget(intro);
    m_settings = new QCheckBox(tr("Sanitized settings"), this); m_settings->setChecked(true);
    m_logs = new QCheckBox(tr("Sanitized application logs"), this); m_logs->setChecked(true);
    m_status = new QCheckBox(tr("Module status summaries"), this); m_status->setChecked(true);
    m_crash = new QCheckBox(tr("Include local crash information (if available)"), this); m_crash->setChecked(false);
    root->addWidget(m_settings); root->addWidget(m_logs); root->addWidget(m_status); root->addWidget(m_crash);
    auto* preview = new QLabel(this); preview->setWordWrap(true); preview->setObjectName(QStringLiteral("supportBundlePreview"));
    root->addWidget(preview);
    auto updatePreview = [this, preview] {
        application::support::SupportBundleOptions options{m_settings->isChecked(), m_logs->isChecked(), m_status->isChecked(), m_crash->isChecked()};
        preview->setText(tr("The archive will contain:\n• %1").arg(m_builder.previewCategories(options).join(QStringLiteral("\n• "))));
    };
    connect(m_settings, &QCheckBox::toggled, this, updatePreview);
    connect(m_logs, &QCheckBox::toggled, this, updatePreview);
    connect(m_status, &QCheckBox::toggled, this, updatePreview);
    connect(m_crash, &QCheckBox::toggled, this, updatePreview);
    updatePreview();
    auto* buttons = new QDialogButtonBox(this);
    auto* create = buttons->addButton(tr("Save Bundle…"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    root->addWidget(buttons);
    connect(create, &QPushButton::clicked, this, &SupportBundleDialog::generate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
void SupportBundleDialog::generate()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Support Bundle"),
        QStringLiteral("DevHub-support-%1.zip").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        tr("ZIP archive (*.zip)"));
    if (path.isEmpty()) return;
    application::support::SupportBundleOptions options{m_settings->isChecked(), m_logs->isChecked(), m_status->isChecked(), m_crash->isChecked()};
    const auto result = m_builder.write(path, options, m_moduleStatus);
    if (!result) { QMessageBox::warning(this, tr("Could not create bundle"), result.error().userMessage); return; }
    QMessageBox::information(this, tr("Support bundle saved"), tr("Saved to %1").arg(path));
    accept();
}
} // namespace devhub::presentation::settings
