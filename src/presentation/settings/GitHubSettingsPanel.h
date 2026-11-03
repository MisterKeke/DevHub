#pragma once

#include "../../application/github/GitHubTypes.h"
#include <QNetworkAccessManager>
#include <QWidget>

class QLabel;
class QPushButton;
namespace devhub::application::github { class GitHubService; }

namespace devhub::presentation::settings {

class GitHubSettingsPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit GitHubSettingsPanel(application::github::GitHubService& service, QWidget* parent = nullptr);

private:
    void updateState(const application::github::GitHubConnection& connection);
    void loadAvatar(const QString& url);
    application::github::GitHubService& m_service;
    QNetworkAccessManager m_avatarNetwork;
    QString m_avatarUrl;
    QLabel* m_status = nullptr;
    QLabel* m_account = nullptr;
    QLabel* m_authMethod = nullptr;
    QLabel* m_tokenStatus = nullptr;
    QLabel* m_lastVerified = nullptr;
    QLabel* m_appStatus = nullptr;
    QLabel* m_avatar = nullptr;
    QPushButton* m_connect = nullptr;
    QPushButton* m_recheck = nullptr;
    QPushButton* m_disconnect = nullptr;
};

} // namespace devhub::presentation::settings
