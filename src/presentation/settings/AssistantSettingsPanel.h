#pragma once
#include "application/assistant/AssistantController.h"
#include "application/assistant/ICodexExecutableProbe.h"
#include <QWidget>
#include <functional>
class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QFormLayout;
namespace devhub::presentation::settings {
class AssistantSettingsPanel final : public QWidget {
    Q_OBJECT
public:
    AssistantSettingsPanel(application::assistant::AssistantController& controller,
        application::assistant::ICodexExecutableProbe& probe,
        application::assistant::AssistantConfiguration configuration, QWidget* parent = nullptr,
        std::function<bool(const QUrl&)> openUrl = {});
    ~AssistantSettingsPanel() override;
    QLineEdit* executableControl() const { return m_executable; }
    QComboBox* modelControl() const { return m_model; }
private:
    void refresh();
    void refreshModels();
    void recheck();
    void startLogin(application::assistant::CodexLoginMode mode);
    application::assistant::AssistantController& m_controller;
    application::assistant::ICodexExecutableProbe& m_probe;
    std::function<bool(const QUrl&)> m_openUrl;
    QLineEdit* m_executable;
    QComboBox* m_model;
    QFormLayout* m_challengeForm;
    QLabel* m_runtime;
    QLabel* m_status;
    QLabel* m_probeStatus;
    QLabel* m_loginStatus;
    QLineEdit* m_url;
    QLineEdit* m_code;
    QPushButton* m_check;
    QPushButton* m_browser;
    QPushButton* m_device;
    QPushButton* m_open;
    QPushButton* m_cancel;
    QPushButton* m_reconnect;
    quint64 m_generation = 0;
    bool m_checking = false;
    bool m_ownsLogin = false;
};
}
