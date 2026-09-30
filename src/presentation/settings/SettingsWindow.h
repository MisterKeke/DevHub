#pragma once
#include <QDialog>
#include <QHash>

class QWidget;
namespace devhub::application::settings { class SettingsService; }
namespace devhub::presentation::theme { class ThemeManager; }
namespace devhub::presentation::settings {
class SettingsWindow final : public QDialog {
    Q_OBJECT
public:
    SettingsWindow(application::settings::SettingsService& settings,
                   presentation::theme::ThemeManager& theme, QString workspaceId = {}, QWidget* parent = nullptr);
signals:
    void changePasswordRequested();
private:
    void saveSettings();
    application::settings::SettingsService& m_settings;
    presentation::theme::ThemeManager& m_theme;
    QString m_workspaceId;
    QHash<QString, QWidget*> m_controls;
};
} // namespace devhub::presentation::settings
