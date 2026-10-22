#include "SettingsWindow.h"
#include "AssistantSettingsPanel.h"
#include "../../application/settings/SettingsService.h"
#include "../../application/settings/SettingTypes.h"
#include "../theme/ThemeManager.h"
#include "../theme/ThemePalette.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace devhub::presentation::settings {
namespace {

QString descriptionFor(const QString& id)
{
    if (id == QStringLiteral("general/restoreLastPage")) return QObject::tr("Return to the page you last used when DevHub starts.");
    if (id == QStringLiteral("general/restoreWindowGeometry")) return QObject::tr("Keep the current window size and position between sessions.");
    if (id == QStringLiteral("security/inactivityTimeoutMinutes")) return QObject::tr("Lock DevHub after the selected period without activity.");
    if (id == QStringLiteral("security/keepSessionDefault")) return QObject::tr("Choose whether a new session stays signed in by default.");
    if (id == QStringLiteral("appearance/theme")) return QObject::tr("Choose the dark accent color used throughout DevHub.");
    if (id == QStringLiteral("appearance/reducedMotion")) return QObject::tr("Reduce animated transitions and visual movement.");
    if (id == QStringLiteral("git/executable")) return QObject::tr("Leave blank to use the Git executable available on PATH.");
    if (id == QStringLiteral("assistant/executable")) return QObject::tr("Set a specific Codex executable, or leave blank to use the detected default.");
    if (id == QStringLiteral("assistant/model")) return QObject::tr("Choose a preferred text model for new Assistant turns.");
    if (id == QStringLiteral("assistant/restoreConversation")) return QObject::tr("Reopen your last DevHub conversation when you return to Assistant.");
    if (id == QStringLiteral("assistant/somethingQtProfilePath")) return QObject::tr("Choose an existing SomethingQt profile database. The Assistant page offers a file picker and an explicit connection check; the suggested default path may not exist on this computer.");
    if (id == QStringLiteral("assistant/somethingQtWorkspaceTag")) return QObject::tr("Convenience default used when a task search omits its tag. It does not restrict task access by itself.");
    if (id == QStringLiteral("assistant/somethingQtRestrictWorkspaceTag")) return QObject::tr("When enabled, searches and detail reads are limited to tasks that also have the configured workspace tag.");
    if (id == QStringLiteral("workspaces/scanLimitMs")) return QObject::tr("Limit how long workspace detection spends scanning each folder.");
    if (id == QStringLiteral("logs/maxLines")) return QObject::tr("Maximum number of log lines kept in memory.");
    if (id == QStringLiteral("logs/maxBytes")) return QObject::tr("Maximum log data kept in memory across sources.");
    if (id == QStringLiteral("ports/refreshIntervalSeconds")) return QObject::tr("How often DevHub refreshes the open port list.");
    if (id == QStringLiteral("database/pageSize")) return QObject::tr("Rows loaded at a time in the database browser.");
    if (id == QStringLiteral("database/queryHistoryEnabled")) return QObject::tr("Keep recent SQL statements for this session only.");
    if (id == QStringLiteral("database/queryHistoryLimit")) return QObject::tr("Maximum number of statements in this session's history.");
    if (id == QStringLiteral("notifications/retentionDays")) return QObject::tr("Remove older notifications after this many days.");
    if (id == QStringLiteral("terminal/profile")) return QObject::tr("Choose the terminal app DevHub opens for a workspace.");
    if (id == QStringLiteral("terminal/customExecutable")) return QObject::tr("Used only when the Custom executable profile is selected.");
    if (id == QStringLiteral("terminal/customArguments")) return QObject::tr("Optional arguments. Use {workspace} or {workspaceName} where needed.");
    return {};
}

QString groupFor(const QString& id)
{
    if (id.startsWith(QStringLiteral("general/"))) return QObject::tr("Startup and window");
    if (id.startsWith(QStringLiteral("security/"))) return QObject::tr("Session security");
    if (id == QStringLiteral("appearance/theme")) return QObject::tr("Theme");
    if (id.startsWith(QStringLiteral("appearance/"))) return QObject::tr("Accessibility");
    if (id.startsWith(QStringLiteral("assistant/")) && id == QStringLiteral("assistant/restoreConversation")) return QObject::tr("Conversation");
    if (id.startsWith(QStringLiteral("assistant/"))) return QObject::tr("Codex connection");
    if (id.startsWith(QStringLiteral("workspaces/"))) return QObject::tr("Workspace scanning");
    if (id.startsWith(QStringLiteral("ports/"))) return QObject::tr("Port monitoring");
    if (id.startsWith(QStringLiteral("logs/"))) return QObject::tr("Log storage");
    if (id == QStringLiteral("database/queryHistoryEnabled") || id == QStringLiteral("database/queryHistoryLimit")) return QObject::tr("Query history");
    if (id.startsWith(QStringLiteral("database/"))) return QObject::tr("Database browser");
    if (id.startsWith(QStringLiteral("notifications/"))) return QObject::tr("Notification history");
    if (id == QStringLiteral("terminal/customExecutable") || id == QStringLiteral("terminal/customArguments")) return QObject::tr("Custom terminal");
    if (id.startsWith(QStringLiteral("terminal/"))) return QObject::tr("Terminal");
    return QObject::tr("Preferences");
}

QString sectionDescription(const QString& section)
{
    if (section == QObject::tr("General")) return QObject::tr("Startup behavior and window preferences.");
    if (section == QObject::tr("Appearance")) return QObject::tr("Tune DevHub's colors and motion.");
    if (section == QObject::tr("Account and Security")) return QObject::tr("Manage your local session and account security.");
    if (section == QObject::tr("Git")) return QObject::tr("Configure the Git executable used by workspace tools.");
    if (section == QObject::tr("Terminal")) return QObject::tr("Choose how workspace terminals are opened.");
    if (section == QObject::tr("Workspaces")) return QObject::tr("Set limits for workspace discovery.");
    if (section == QObject::tr("Services")) return QObject::tr("Configure background service and port monitoring.");
    if (section == QObject::tr("Logs")) return QObject::tr("Control the amount of log output kept in memory.");
    if (section == QObject::tr("Database")) return QObject::tr("Set database browsing and query history preferences.");
    if (section == QObject::tr("Notifications")) return QObject::tr("Choose how long notification history is retained.");
    if (section == QObject::tr("Assistant")) return QObject::tr("Configure Codex, connection, and conversation behavior.");
    if (section == QObject::tr("Advanced")) return QObject::tr("Additional application preferences.");
    return QObject::tr("DevHub version and application information.");
}

QFrame* makePanel(const QString& title, QWidget* parent, QVBoxLayout** contentLayout)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("settingsPanel"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    auto* heading = new QLabel(title, panel);
    heading->setObjectName(QStringLiteral("settingsPanelTitle"));
    layout->addWidget(heading);
    auto* line = new QFrame(panel);
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: %1; border: 0;")
        .arg(theme::ThemePalette::color(theme::ColorToken::BorderSubtle).name()));
    layout->addWidget(line);
    *contentLayout = layout;
    return panel;
}

} // namespace

SettingsWindow::SettingsWindow(application::settings::SettingsService& settings,
                               presentation::theme::ThemeManager& theme, QString workspaceId, QWidget* parent,
                               application::assistant::AssistantController* assistant,
                               application::assistant::ICodexExecutableProbe* probe)
    : QDialog(parent), m_settings(settings), m_theme(theme), m_workspaceId(std::move(workspaceId))
{
    setWindowTitle(tr("DevHub Settings"));
    setMinimumSize(780, 560);
    resize(1040, 720);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    const auto values = m_settings.effectiveValues(m_workspaceId);
    const QStringList sections{tr("General"), tr("Appearance"), tr("Account and Security"), tr("Git"),
        tr("Terminal"), tr("Workspaces"), tr("Services"), tr("Logs"), tr("Database"),
        tr("Notifications"), tr("Assistant"), tr("Advanced"), tr("About")};

    auto* sidebar = new QWidget(body);
    sidebar->setObjectName(QStringLiteral("settingsSidebar"));
    sidebar->setFixedWidth(214);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 20, 12, 14);
    sidebarLayout->setSpacing(12);
    auto* sidebarTitle = new QLabel(tr("SETTINGS"), sidebar);
    sidebarTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 9pt; font-weight: 600;")
        .arg(theme::ThemePalette::color(theme::ColorToken::TextMuted).name()));
    sidebarLayout->addWidget(sidebarTitle);
    auto* navigation = new QListWidget(sidebar);
    navigation->setObjectName(QStringLiteral("settingsNavigation"));
    navigation->setAccessibleName(tr("Settings categories"));
    navigation->setFrameShape(QFrame::NoFrame);
    navigation->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const auto& section : sections) navigation->addItem(section);
    sidebarLayout->addWidget(navigation, 1);
    bodyLayout->addWidget(sidebar);

    auto* contentColumn = new QWidget(body);
    auto* contentLayout = new QVBoxLayout(contentColumn);
    contentLayout->setContentsMargins(24, 20, 24, 0);
    contentLayout->setSpacing(14);
    auto* pageTitle = new QLabel(contentColumn);
    pageTitle->setObjectName(QStringLiteral("settingsPageTitle"));
    auto* pageDescription = new QLabel(contentColumn);
    pageDescription->setObjectName(QStringLiteral("settingsPageDescription"));
    pageDescription->setWordWrap(true);
    contentLayout->addWidget(pageTitle);
    contentLayout->addWidget(pageDescription);

    auto* pageStack = new QStackedWidget(contentColumn);
    pageStack->setObjectName(QStringLiteral("settingsPageStack"));
    contentLayout->addWidget(pageStack, 1);
    bodyLayout->addWidget(contentColumn, 1);
    root->addWidget(body, 1);

    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("settingsFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 12, 20, 12);
    footerLayout->setSpacing(12);
    m_changeState = new QLabel(tr("All changes saved"), footer);
    m_changeState->setObjectName(QStringLiteral("settingsDirtyState"));
    footerLayout->addWidget(m_changeState, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, footer);
    m_saveButton = buttons->button(QDialogButtonBox::Save);
    m_saveButton->setObjectName(QStringLiteral("saveSettingsButton"));
    m_saveButton->setText(tr("Save Changes"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("cancelSettingsButton"));
    footerLayout->addWidget(buttons);
    root->addWidget(footer);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsWindow::saveSettings);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto descriptors = m_settings.descriptors();
    for (const auto& section : sections) {
        auto* scroll = new QScrollArea(pageStack);
        scroll->setObjectName(QStringLiteral("settingsPageScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* page = new QWidget(scroll);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(2, 2, 12, 20);
        pageLayout->setSpacing(12);
        QHash<QString, QVBoxLayout*> panelLayouts;
        const auto panelFor = [page, pageLayout, &panelLayouts](const QString& title) {
            if (panelLayouts.contains(title)) return panelLayouts.value(title);
            QVBoxLayout* panelLayout = nullptr;
            auto* panel = makePanel(title, page, &panelLayout);
            pageLayout->addWidget(panel);
            panelLayouts.insert(title, panelLayout);
            return panelLayout;
        };

        if (section == tr("Assistant") && assistant && probe) {
            auto* setup = new AssistantSettingsPanel(*assistant, *probe,
                {values.value(QStringLiteral("assistant/executable")).toString(), values.value(QStringLiteral("assistant/model")).toString()}, page);
            panelFor(tr("Codex connection"))->addWidget(setup);
            m_controls.insert(QStringLiteral("assistant/executable"), setup->executableControl());
            m_controls.insert(QStringLiteral("assistant/model"), setup->modelControl());
        }
        if (section == tr("Account and Security")) {
            auto* panelLayout = panelFor(tr("Password and session"));
            auto* password = new QPushButton(tr("Change Password…"), page);
            password->setObjectName(QStringLiteral("changePasswordButton"));
            password->setAccessibleName(tr("Change account password"));
            connect(password, &QPushButton::clicked, this, &SettingsWindow::changePasswordRequested);
            panelLayout->addWidget(password, 0, Qt::AlignLeft);
            auto* safety = new QLabel(tr("Security confirmations for destructive actions cannot be disabled globally."), page);
            safety->setWordWrap(true);
            safety->setStyleSheet(QStringLiteral("color: %1;").arg(theme::ThemePalette::color(theme::ColorToken::TextSecondary).name()));
            panelLayout->addWidget(safety);
        }
        if (section == tr("About")) {
            auto* panelLayout = panelFor(tr("About DevHub"));
            auto* about = new QLabel(tr("DevHub %1\nA local workspace companion.").arg(QCoreApplication::applicationVersion()), page);
            about->setWordWrap(true);
            panelLayout->addWidget(about);
        }

        for (const auto& descriptor : descriptors) {
            if (descriptor.section != section || m_controls.contains(descriptor.id)) continue;
            QWidget* control = nullptr;
            const QVariant value = values.value(descriptor.id, descriptor.defaultValue);
            if (descriptor.id == QStringLiteral("appearance/theme")) {
                auto* choice = new QComboBox(page);
                choice->addItem(tr("Dark rose"), QStringLiteral("dark-rose"));
                choice->addItem(tr("Dark blue"), QStringLiteral("dark-blue"));
                choice->setCurrentIndex(qMax(0, choice->findData(value.toString())));
                control = choice;
            } else if (descriptor.id == QStringLiteral("terminal/profile")) {
                auto* choice = new QComboBox(page);
                choice->addItem(tr("Windows Terminal"), QStringLiteral("windows-terminal"));
                choice->addItem(tr("PowerShell"), QStringLiteral("powershell"));
                choice->addItem(tr("Command Prompt"), QStringLiteral("cmd"));
                choice->addItem(tr("WSL (optional)"), QStringLiteral("wsl"));
                choice->addItem(tr("Custom executable"), QStringLiteral("custom"));
                choice->setCurrentIndex(qMax(0, choice->findData(value.toString())));
                control = choice;
            } else if (value.metaType().id() == QMetaType::Bool) {
                auto* check = new QCheckBox(page);
                check->setChecked(value.toBool());
                control = check;
            } else if (value.metaType().id() == QMetaType::Int) {
                auto* spin = new QSpinBox(page);
                spin->setRange(0, 67108864);
                spin->setValue(value.toInt());
                if (descriptor.id.contains(QStringLiteral("Minutes"))) spin->setSuffix(tr(" min"));
                else if (descriptor.id.contains(QStringLiteral("Seconds"))) spin->setSuffix(tr(" sec"));
                else if (descriptor.id == QStringLiteral("workspaces/scanLimitMs")) spin->setSuffix(tr(" ms"));
                else if (descriptor.id == QStringLiteral("logs/maxBytes")) spin->setSuffix(tr(" bytes"));
                else if (descriptor.id == QStringLiteral("logs/maxLines")) spin->setSuffix(tr(" lines"));
                control = spin;
            } else {
                auto* edit = new QLineEdit(value.toString(), page);
                if (descriptor.id == QStringLiteral("terminal/customArguments"))
                    edit->setPlaceholderText(tr("Example: --directory {workspace}"));
                if (descriptor.sensitivity == application::settings::SettingSensitivity::Sensitive)
                    edit->setEchoMode(QLineEdit::Normal);
                control = edit;
            }

            control->setObjectName(descriptor.id);
            control->setMinimumWidth(qobject_cast<QCheckBox*>(control) ? 0 : 190);
            m_controls.insert(descriptor.id, control);
            m_initialValues.insert(descriptor.id, value);

            auto* row = new QWidget(page);
            row->setObjectName(QStringLiteral("settingsRow"));
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 3, 0, 8);
            rowLayout->setSpacing(16);
            auto* textColumn = new QVBoxLayout;
            textColumn->setContentsMargins(0, 0, 0, 0);
            textColumn->setSpacing(3);
            auto* label = new QLabel(descriptor.requiresRestart
                ? tr("%1 (restart to apply)").arg(descriptor.label) : descriptor.label, row);
            label->setWordWrap(true);
            auto* description = new QLabel(descriptionFor(descriptor.id), row);
            description->setObjectName(QStringLiteral("settingsFieldDescription"));
            description->setWordWrap(true);
            textColumn->addWidget(label);
            textColumn->addWidget(description);
            QString scopeText;
            if (descriptor.scope == application::settings::SettingScope::Workspace)
                scopeText = tr("Stored for this workspace.");
            else if (descriptor.scope == application::settings::SettingScope::Session)
                scopeText = tr("Applies for this session.");
            if (!scopeText.isEmpty()) {
                auto* scope = new QLabel(scopeText, row);
                scope->setObjectName(QStringLiteral("settingsFieldDescription"));
                textColumn->addWidget(scope);
            }
            if (descriptor.requiresRestart) {
                auto* restart = new QLabel(tr("Requires restart"), row);
                restart->setObjectName(QStringLiteral("settingsFieldDescription"));
                textColumn->addWidget(restart);
            }
            rowLayout->addLayout(textColumn, 1);
            auto* right = new QVBoxLayout;
            right->setContentsMargins(0, 0, 0, 0);
            right->setSpacing(4);
            right->addWidget(control, 0, Qt::AlignRight);
            auto* state = new QLabel(row);
            state->setObjectName(QStringLiteral("settingsValueState"));
            state->setAlignment(Qt::AlignRight);
            right->addWidget(state);
            rowLayout->addLayout(right);
            panelFor(groupFor(descriptor.id))->addWidget(row);
            m_valueStates.insert(descriptor.id, state);

            if (auto* check = qobject_cast<QCheckBox*>(control))
                connect(check, &QCheckBox::toggled, this, &SettingsWindow::updateChangeState);
            else if (auto* spin = qobject_cast<QSpinBox*>(control))
                connect(spin, &QSpinBox::valueChanged, this, &SettingsWindow::updateChangeState);
            else if (auto* combo = qobject_cast<QComboBox*>(control))
                connect(combo, &QComboBox::currentIndexChanged, this, &SettingsWindow::updateChangeState);
            else if (auto* edit = qobject_cast<QLineEdit*>(control))
                connect(edit, &QLineEdit::textChanged, this, &SettingsWindow::updateChangeState);
        }

        if (panelLayouts.isEmpty()) {
            QVBoxLayout* emptyPanelLayout = nullptr;
            auto* emptyPanel = makePanel(tr("Nothing to configure"), page, &emptyPanelLayout);
            auto* emptyMessage = new QLabel(tr("There are no additional preferences in this section."), page);
            emptyMessage->setWordWrap(true);
            emptyPanelLayout->addWidget(emptyMessage);
            pageLayout->addWidget(emptyPanel);
        }
        pageLayout->addStretch();
        scroll->setWidget(page);
        pageStack->addWidget(scroll);
    }

    connect(navigation, &QListWidget::currentRowChanged, this, [pageStack, pageTitle, pageDescription, sections](int row) {
        if (row < 0 || row >= sections.size()) return;
        const QString section = sections.at(row);
        pageStack->setCurrentIndex(row);
        pageTitle->setText(section);
        pageDescription->setText(sectionDescription(section));
    });
    navigation->setCurrentRow(0);

    for (auto i = m_controls.cbegin(); i != m_controls.cend(); ++i) {
        if (m_initialValues.contains(i.key())) continue;
        const auto descriptor = m_settings.descriptor(i.key());
        m_initialValues.insert(i.key(), values.value(i.key(), descriptor.defaultValue));
        if (auto* edit = qobject_cast<QLineEdit*>(i.value()))
            connect(edit, &QLineEdit::textChanged, this, &SettingsWindow::updateChangeState);
        else if (auto* combo = qobject_cast<QComboBox*>(i.value()))
            connect(combo, &QComboBox::currentIndexChanged, this, &SettingsWindow::updateChangeState);
    }
    updateChangeState();
}

QVariant SettingsWindow::valueForControl(QWidget* control) const
{
    if (auto* check = qobject_cast<QCheckBox*>(control)) return check->isChecked();
    if (auto* spin = qobject_cast<QSpinBox*>(control)) return spin->value();
    if (auto* combo = qobject_cast<QComboBox*>(control)) return combo->currentData();
    if (auto* edit = qobject_cast<QLineEdit*>(control)) return edit->text();
    return {};
}

void SettingsWindow::updateChangeState()
{
    bool changed = false;
    for (auto i = m_controls.cbegin(); i != m_controls.cend(); ++i) {
        const QVariant value = valueForControl(i.value());
        const bool modified = value != m_initialValues.value(i.key());
        changed = changed || modified;
        if (auto* state = m_valueStates.value(i.key(), nullptr)) {
            const auto descriptor = m_settings.descriptor(i.key());
            state->setText(modified ? tr("Modified")
                : value == descriptor.defaultValue ? tr("Default") : tr("Saved"));
        }
    }
    m_changeState->setText(changed ? tr("Unsaved changes") : tr("All changes saved"));
    m_saveButton->setEnabled(changed);
}

void SettingsWindow::saveSettings()
{
    application::settings::SettingValues pending;
    for (auto i = m_controls.cbegin(); i != m_controls.cend(); ++i) {
        const QVariant value = valueForControl(i.value());
        const auto descriptor = m_settings.descriptor(i.key());
        if (!descriptor.validate || !descriptor.validate(value)) {
            QMessageBox::warning(this, tr("Could not save settings"), tr("That setting value is invalid: %1").arg(descriptor.label));
            return;
        }
        pending.insert(i.key(), value);
    }
    const auto saved = m_settings.setValues(pending, m_workspaceId);
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
    const QString theme = m_settings.value(QStringLiteral("appearance/theme")).toString();
    m_theme.setVariant(theme == QStringLiteral("dark-blue")
        ? presentation::theme::ThemeVariant::DarkBlue
        : presentation::theme::ThemeVariant::DarkRose);
    m_theme.setReducedMotion(m_settings.value(QStringLiteral("appearance/reducedMotion")).toBool());
    emit settingsApplied();
    accept();
}

} // namespace devhub::presentation::settings
