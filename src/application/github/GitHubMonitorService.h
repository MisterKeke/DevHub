#pragma once

#include "GitHubTypes.h"

#include <QObject>
#include <QTimer>

#include <optional>

namespace devhub::application::github { class GitHubService; }
namespace devhub::application::settings { class SettingsService; }
namespace devhub::application::notifications { class NotificationService; }

namespace devhub::application::github {

class GitHubMonitorService final : public QObject
{
    Q_OBJECT
public:
    GitHubMonitorService(GitHubService& github, settings::SettingsService& settings,
                         notifications::NotificationService& notifications, QObject* parent = nullptr);

    void setTarget(QString workspaceId, GitHubRepository repository);
    void clearTarget();
    void configurationChanged();

private:
    void updateSchedule();
    void poll();
    void processSnapshot(GitHubMonitorSnapshot snapshot, QString error);

    GitHubService& m_github;
    settings::SettingsService& m_settings;
    notifications::NotificationService& m_notifications;
    QTimer m_timer;
    QString m_workspaceId;
    std::optional<GitHubRepository> m_repository;
    bool m_inFlight = false;
    quint64 m_targetGeneration = 0;
};

} // namespace devhub::application::github
