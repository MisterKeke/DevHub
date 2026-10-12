#include "SettingsWindow.h"
#include "AssistantSettingsPanel.h"
#include "../../application/settings/SettingsService.h"
#include "../theme/ThemeManager.h"
#include "../theme/ThemePalette.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace devhub::presentation::settings {

SettingsWindow::SettingsWindow(application::settings::SettingsService& settings,
                               presentation::theme::ThemeManager& theme, QString workspaceId, QWidget* parent,
                               application::assistant::AssistantController* assistant,
                               application::assistant::ICodexExecutableProbe* probe)
    : QDialog(parent), m_settings(settings), m_theme(theme), m_workspaceId(std::move(workspaceId))
{
    setWindowTitle(tr("DevHub Settings"));
    setMinimumSize(780, 560);
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    const QStringList sections{tr("General"), tr("Appearance"), tr("Account and Security"), tr("Git"),
        tr("Terminal"), tr("Workspaces"), tr("Services"), tr("Logs"), tr("Database"),
        tr("Notifications"), tr("Assistant"), tr("Advanced"), tr("About")};
    const auto values = m_settings.effectiveValues(m_workspaceId);
    QHash<QString, QFormLayout*> forms;
    for (const QString& section : sections) {
        auto* page = new QWidget(tabs);
        auto* layout = new QVBoxLayout(page);
        auto* formWidget = new QWidget(page);
        auto* form = new QFormLayout(formWidget);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addWidget(formWidget);
        if (section == tr("Assistant") && assistant && probe) {
            auto* setup = new AssistantSettingsPanel(*assistant, *probe,
                {values.value(QStringLiteral("assistant/executable")).toString(), values.value(QStringLiteral("assistant/model")).toString()}, page);
            layout->addWidget(setup);
            m_controls.insert(QStringLiteral("assistant/executable"), setup->executableControl());
            m_controls.insert(QStringLiteral("assistant/model"), setup->modelControl());
        }
        if (section == tr("About")) {
            auto* about = new QLabel(tr("DevHub %1\nA local workspace companion.").arg(QCoreApplication::applicationVersion()), page);
            about->setWordWrap(true); layout->addWidget(about);
        }
        if (section == tr("Account and Security")) {
            auto* password = new QPushButton(tr("Change Password…"), page);
            connect(password, &QPushButton::clicked, this, &SettingsWindow::changePasswordRequested);
            layout->addWidget(password, 0, Qt::AlignLeft);
            auto* safety = new QLabel(tr("Security confirmations for destructive actions cannot be disabled globally."), page);
            safety->setWordWrap(true); layout->addWidget(safety);
        }
        layout->addStretch();
        tabs->addTab(page, section);
        forms.insert(section, form);
    }

    for (const auto& descriptor : m_settings.descriptors()) {
        if (m_controls.contains(descriptor.id)) continue;
        QWidget* control = nullptr;
        const QVariant value = values.value(descriptor.id, descriptor.defaultValue);
        if (descriptor.id == QStringLiteral("appearance/theme")) {
            auto* choice = new QComboBox(this);
            choice->addItem(tr("Dark rose"), QStringLiteral("dark-rose"));
            choice->addItem(tr("Dark blue"), QStringLiteral("dark-blue"));
            choice->setCurrentIndex(qMax(0, choice->findData(value.toString())));
            control = choice;
        } else if (descriptor.id == QStringLiteral("terminal/profile")) {
            auto* choice = new QComboBox(this);
            choice->addItem(tr("Windows Terminal"), QStringLiteral("windows-terminal"));
            choice->addItem(tr("PowerShell"), QStringLiteral("powershell"));
            choice->addItem(tr("Command Prompt"), QStringLiteral("cmd"));
            choice->addItem(tr("WSL (optional)"), QStringLiteral("wsl"));
            choice->addItem(tr("Custom executable"), QStringLiteral("custom"));
            choice->setCurrentIndex(qMax(0, choice->findData(value.toString())));
            control = choice;
        } else if (value.metaType().id() == QMetaType::Bool) {
            auto* check = new QCheckBox(this); check->setChecked(value.toBool()); control = check;
        } else if (value.metaType().id() == QMetaType::Int) {
            auto* spin = new QSpinBox(this); spin->setRange(0, 67108864); spin->setValue(value.toInt());
            if (descriptor.id.contains(QStringLiteral("Minutes"))) spin->setSuffix(tr(" min"));
            else if (descriptor.id.contains(QStringLiteral("Seconds"))) spin->setSuffix(tr(" sec"));
            control = spin;
        } else {
            auto* edit = new QLineEdit(value.toString(), this);
            if (descriptor.id == QStringLiteral("terminal/customArguments"))
                edit->setPlaceholderText(tr("Example: --directory {workspace}"));
            control = edit;
        }
        const QString displayLabel = descriptor.requiresRestart
            ? tr("%1 (restart to apply)").arg(descriptor.label) : descriptor.label;
        if (auto* form = forms.value(descriptor.section)) form->addRow(displayLabel, control);
        control->setObjectName(descriptor.id); m_controls.insert(descriptor.id, control);
    }
    root->addWidget(tabs, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsWindow::saveSettings);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsWindow::saveSettings()
{
    application::settings::SettingValues pending;
    for (auto i = m_controls.cbegin(); i != m_controls.cend(); ++i) {
        QVariant value;
        if (auto* check = qobject_cast<QCheckBox*>(i.value())) value = check->isChecked();
        else if (auto* spin = qobject_cast<QSpinBox*>(i.value())) value = spin->value();
        else if (auto* combo = qobject_cast<QComboBox*>(i.value())) value = combo->currentData();
        else if (auto* edit = qobject_cast<QLineEdit*>(i.value())) value = edit->text();
        const auto descriptor = m_settings.descriptor(i.key());
        if (!descriptor.validate || !descriptor.validate(value)) {
            QMessageBox::warning(this, tr("Could not save settings"), tr("That setting value is invalid: %1").arg(descriptor.label)); return;
        }
        pending.insert(i.key(), value);
    }
    for (auto i = pending.cbegin(); i != pending.cend(); ++i) {
        const auto saved = m_settings.setValue(i.key(), i.value(), m_workspaceId);
        if (!saved) {
            auto* message = new QMessageBox(this);
            message->setIcon(QMessageBox::Warning);
            message->setStandardButtons(QMessageBox::Ok);
            message->setWindowTitle(tr("Could not save settings"));
            message->setText(saved.error().userMessage);
            message->setDetailedText(saved.error().technicalMessage);
            message->setAttribute(Qt::WA_DeleteOnClose);
            message->open();
            return;
        }
    }
    const QString theme = m_settings.value(QStringLiteral("appearance/theme")).toString();
    m_theme.setVariant(theme == QStringLiteral("dark-blue")
        ? presentation::theme::ThemeVariant::DarkBlue
        : presentation::theme::ThemeVariant::DarkRose);
    m_theme.setReducedMotion(m_settings.value(QStringLiteral("appearance/reducedMotion")).toBool());
    emit settingsApplied();
    accept();
}

} // namespace devhub::presentation::settings
