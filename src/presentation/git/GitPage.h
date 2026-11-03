#pragma once
#include "../../application/tasks/TaskScheduler.h"
#include "../../application/git/GitTypes.h"
#include "../../application/github/GitHubTypes.h"
#include <QWidget>
#include <QUuid>
#include <optional>
#include <QVector>

class QLabel; class QListWidget; class QPushButton; class QPlainTextEdit; class QStackedWidget; class QTabWidget;
class QShowEvent; class QSplitter;
namespace devhub::application::settings { class SettingsService; }
namespace devhub::application::github { class GitHubService; }
namespace devhub::application::planner { class PlannerService; }
namespace devhub::presentation::git {
class GitHubPullRequestsPanel;
class GitPage final : public QWidget {
    Q_OBJECT
public:
    explicit GitPage(application::tasks::TaskScheduler& scheduler, QWidget* parent = nullptr,
                      application::settings::SettingsService* settings = nullptr,
                      application::github::GitHubService* github = nullptr,
                      application::planner::PlannerService* planner = nullptr);
    void setWorkspacePath(QString path, std::optional<QUuid> workspaceId = std::nullopt);
    void focusBranch(QString branch);
    void focusCommit(QString commitSha);
    void refreshNow();
signals:
    void operationFailed(QString title, QString message);
    void overviewUpdated(QString summary, QStringList details, QString statusLabel, QString statusTone);
    void openPlannerTaskRequested(QString taskId);
    void plannerTaskLinked(QString taskId);
    void githubMonitorTargetChanged(QString workspaceId, QString remoteName, QString owner, QString name);
protected:
    void showEvent(QShowEvent* event) override;
private:
    void refresh();
    void loadOlderHistory();
    void appendCommit(const application::git::GitCommit& commit);
    void showDiff(QString commit = {}, bool staged = false);
    void setBusy(bool busy);
    void runMutation(QString operation, QStringList arguments);
    void requestPushPreview(bool publishBranch);
    void preparePush(QString remote, QString targetBranch, bool establishUpstream, quint64 generation);
    void executePush(const application::git::GitPushPreview& preview);
    [[nodiscard]] QString pushPreviewText(const application::git::GitPushPreview& preview) const;
    application::tasks::TaskScheduler& m_scheduler;
    application::settings::SettingsService* m_settings = nullptr;
    QString m_path;
    QString m_requestedBranch;
    QString m_requestedCommit;
    std::optional<QUuid> m_workspaceId;
    quint64 m_statusGeneration = 0;
    quint64 m_diffGeneration = 0;
    quint64 m_historyGeneration = 0;
    application::tasks::TaskHandle m_activeTask;
    application::tasks::TaskHandle m_diffTask;
    application::tasks::TaskHandle m_historyTask;
    QString m_operationFeedback;
    QLabel* m_state = nullptr;
    QLabel* m_partialErrors = nullptr;
    QPushButton* m_refresh = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_push = nullptr;
    QPushButton* m_publishBranch = nullptr;
    QVector<QPushButton*> m_mutationButtons;
    QListWidget* m_status = nullptr;
    QListWidget* m_history = nullptr;
    QPushButton* m_loadMore = nullptr;
    QListWidget* m_branches = nullptr;
    QPlainTextEdit* m_diff = nullptr;
    QTabWidget* m_tabs = nullptr;
    QSplitter* m_split = nullptr;
    GitHubPullRequestsPanel* m_pullRequests = nullptr;
};
}
