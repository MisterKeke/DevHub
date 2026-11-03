#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QPushButton;
namespace devhub::application::github { class GitHubService; }

namespace devhub::presentation::settings {

class GitHubDeviceFlowDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit GitHubDeviceFlowDialog(application::github::GitHubService& service, QWidget* parent = nullptr);

private:
    void setAuthorizationCode(const QString& userCode, const QUrl& verificationUri, int expiresInSeconds);
    application::github::GitHubService& m_service;
    QLabel* m_code = nullptr;
    QLabel* m_url = nullptr;
    QLabel* m_state = nullptr;
    QLabel* m_countdown = nullptr;
    QPushButton* m_open = nullptr;
    QPushButton* m_finish = nullptr;
    QUrl m_verificationUri;
};

} // namespace devhub::presentation::settings
