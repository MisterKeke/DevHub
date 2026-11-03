#pragma once

#include "../../application/github/GitHubTypes.h"

#include <QWidget>
#include <QUuid>
#include <optional>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;
namespace devhub::application::github { class GitHubService; }
namespace devhub::application::settings { class SettingsService; }
namespace devhub::application::planner { class PlannerService; }
namespace devhub::presentation::widgets { class SafeMarkdownBrowser; }

namespace devhub::presentation::git {

class GitHubPullRequestsPanel final : public QWidget
{
    Q_OBJECT
public:
    GitHubPullRequestsPanel(application::github::GitHubService& service,
                            application::settings::SettingsService* settings,
                            application::planner::PlannerService* planner,
                            QWidget* parent = nullptr);
    void setWorkspace(std::optional<QUuid> workspaceId);
    void setRepositories(QVector<application::github::GitHubRepository> repositories);
    void setRepositoryError(QString error);
    void setLocalBranch(QString branch);
    void refresh();

signals:
    void openPlannerTaskRequested(QString taskId);
    void plannerTaskLinked(QString taskId);
    void githubMonitorTargetChanged(QString workspaceId, QString remoteName, QString owner, QString name);

private:
    void loadPage(int page);
    void loadDetails(int number);
    void renderDetails(const application::github::GitHubPullRequestDetails& details);
    void filterRows();
    void updateSelectedStatus(const application::github::GitHubPullRequestDetails& details);
    void linkToPlannerTask();
    void notifyMonitorTarget();
    void updateSummary();
    [[nodiscard]] std::optional<application::github::GitHubRepository> selectedRepository() const;
    [[nodiscard]] QString workspaceKey() const;

    application::github::GitHubService& m_service;
    application::settings::SettingsService* m_settings = nullptr;
    application::planner::PlannerService* m_planner = nullptr;
    std::optional<QUuid> m_workspaceId;
    QVector<application::github::GitHubRepository> m_repositories;
    QVector<application::github::GitHubPullRequest> m_pullRequests;
    QComboBox* m_repository = nullptr;
    QComboBox* m_stateFilter = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_refresh = nullptr;
    QPushButton* m_loadMore = nullptr;
    QPushButton* m_openOnGitHub = nullptr;
    QPushButton* m_linkTask = nullptr;
    QLabel* m_message = nullptr;
    QLabel* m_connectionStatus = nullptr;
    QVector<QLabel*> m_summaryValues;
    QTableWidget* m_table = nullptr;
    QLabel* m_detailHeading = nullptr;
    QTabWidget* m_detailTabs = nullptr;
    presentation::widgets::SafeMarkdownBrowser* m_overview = nullptr;
    QListWidget* m_files = nullptr;
    QPlainTextEdit* m_filePatch = nullptr;
    QListWidget* m_reviews = nullptr;
    presentation::widgets::SafeMarkdownBrowser* m_reviewBody = nullptr;
    QListWidget* m_checks = nullptr;
    QPlainTextEdit* m_checkDetails = nullptr;
    QPushButton* m_loadJobLogs = nullptr;
    QPushButton* m_openCheckLink = nullptr;
    QLabel* m_ciHistoryMessage = nullptr;
    QTableWidget* m_ciHistoryTable = nullptr;
    QListWidget* m_ciHistoryJobs = nullptr;
    QPlainTextEdit* m_ciHistoryDetails = nullptr;
    QPushButton* m_ciHistoryMore = nullptr;
    QPushButton* m_loadHistoryJobLogs = nullptr;
    QPushButton* m_openHistoryJobLink = nullptr;
    int m_ciHistoryPage = 0;
    bool m_ciHistoryHasMore = false;
    qint64 m_currentHistoryRunId = 0;
    qint64 m_checkLogJobId = 0;
    qint64 m_historyLogJobId = 0;
    int m_currentPage = 0;
    bool m_hasMore = false;
    int m_selectedNumber = 0;
    QString m_localBranch;
    QListWidget* m_linkedTasks = nullptr;
    QPushButton* m_openLinkedTask = nullptr;
    quint64 m_uiGeneration = 0;
};

} // namespace devhub::presentation::git
