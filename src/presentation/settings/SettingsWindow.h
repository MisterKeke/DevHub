#pragma once
#include <QDialog>
#include <QHash>

class QWidget;
namespace devhub::application::settings { class SettingsService; }
namespace devhub::presentation::theme { class ThemeManager; }
namespace devhub::application::assistant { class AssistantController; class ICodexExecutableProbe; }
namespace devhub::presentation::settings {
class SettingsWindow final : public QDialog {
    Q_OBJECT
public:
    SettingsWindow(application::settings::SettingsService& settings,
                   presentation::theme::ThemeManager& theme, QString workspaceId = {}, QWidget* parent = nullptr,
                   application::assistant::AssistantController* assistant = nullptr,
                   application::assistant::ICodexExecutableProbe* probe = nullptr);
signals:
    void changePasswordRequested();
    void settingsApplied();
private:
    void saveSettings();
    application::settings::SettingsService& m_settings;
    presentation::theme::ThemeManager& m_theme;
    QString m_workspaceId;
    QHash<QString, QWidget*> m_controls;
};
} // namespace devhub::presentation::settings
