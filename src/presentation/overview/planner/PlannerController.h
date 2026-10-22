#pragma once

#include "PlannerTaskModel.h"
#include "../../../application/planner/PlannerService.h"
#include "../../../application/tasks/TaskScheduler.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <array>
#include <optional>

namespace devhub::application::settings { class SettingsService; }
namespace devhub::application::workspaces { class WorkspaceContext; }

namespace devhub::presentation::overview::planner {

class PlannerController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PlannerTaskModel* tasks READ tasks CONSTANT)
    Q_PROPERTY(QAbstractItemModel* listModel READ listModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* calendarModel READ calendarModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* undatedModel READ undatedModel CONSTANT)
    Q_PROPERTY(QString workspaceId READ workspaceId NOTIFY changed)
    Q_PROPERTY(QString workspaceName READ workspaceName NOTIFY changed)
    Q_PROPERTY(bool hasWorkspace READ hasWorkspace NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString view READ view NOTIFY changed)
    Q_PROPERTY(bool collapsed READ collapsed NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(int inProgressCount READ inProgressCount NOTIFY changed)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY changed)
    Q_PROPERTY(int overdueCount READ overdueCount NOTIFY changed)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY changed)
    Q_PROPERTY(QString nearestDeadline READ nearestDeadline NOTIFY changed)
    Q_PROPERTY(QString selectedDate READ selectedDate NOTIFY changed)
    Q_PROPERTY(QVariantList milestones READ milestones NOTIFY changed)
    Q_PROPERTY(QVariantList branchOptions READ branchOptions NOTIFY changed)
    Q_PROPERTY(bool branchLoading READ branchLoading NOTIFY changed)
    Q_PROPERTY(QString branchStatus READ branchStatus NOTIFY changed)
    Q_PROPERTY(QString milestoneFilter READ milestoneFilter NOTIFY changed)
    Q_PROPERTY(QString gitBranchFilter READ gitBranchFilter NOTIFY changed)
    Q_PROPERTY(int blockedFilter READ blockedFilter NOTIFY changed)
    Q_PROPERTY(int overdueFilter READ overdueFilter NOTIFY changed)
    Q_PROPERTY(QString originFilter READ originFilter NOTIFY changed)
public:
    PlannerController(application::planner::PlannerService& service,
                       application::settings::SettingsService& settings,
                       application::workspaces::WorkspaceContext& workspaceContext,
                       QObject* parent = nullptr,
                       application::tasks::TaskScheduler* scheduler = nullptr);
    PlannerTaskModel* tasks() noexcept { return &m_tasks; }
    QAbstractItemModel* listModel() noexcept { return &m_list; }
    QAbstractItemModel* calendarModel() noexcept { return &m_calendar; }
    QAbstractItemModel* undatedModel() noexcept { return &m_undated; }
    QString workspaceId() const { return m_workspaceId; }
    QString workspaceName() const { return m_workspaceName; }
    bool hasWorkspace() const { return !m_workspaceId.isEmpty() && m_sessionAllowed; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    QString view() const { return m_view; }
    bool collapsed() const { return m_collapsed; }
    int totalCount() const;
    int inProgressCount() const;
    int completedCount() const;
    int overdueCount() const;
    int progressPercent() const;
    QString nearestDeadline() const;
    QString selectedDate() const { return m_selectedDate; }
    QVariantList milestones() const { return m_milestones; }
    QVariantList branchOptions() const { return m_branchOptions; }
    bool branchLoading() const { return m_branchLoading; }
    QString branchStatus() const { return m_branchStatus; }
    QString milestoneFilter() const { return m_milestoneFilter; }
    QString gitBranchFilter() const { return m_gitBranchFilter; }
    int blockedFilter() const { return m_blockedFilter; }
    int overdueFilter() const { return m_overdueFilter; }
    QString originFilter() const { return m_originFilter; }
    Q_INVOKABLE QAbstractItemModel* boardModel(const QString& status) const;
    Q_INVOKABLE int statusCount(const QString& status) const;
    Q_INVOKABLE QVariantMap taskDetails(const QString& taskId) const;
    Q_INVOKABLE QVariantList dependencyChoices(const QString& excludeTaskId) const;
    Q_INVOKABLE int countForDate(const QString& isoDate) const;
    Q_INVOKABLE void setView(const QString& value);
    Q_INVOKABLE void setCollapsed(bool value);
    Q_INVOKABLE void setSearchText(const QString& value);
    Q_INVOKABLE void setStatusFilter(const QString& value);
    Q_INVOKABLE void setPriorityFilter(const QString& value);
    Q_INVOKABLE void setCompletionFilter(int value);
    Q_INVOKABLE void setTagFilter(const QString& value);
    Q_INVOKABLE void setMilestoneFilter(const QString& value);
    Q_INVOKABLE void setGitBranchFilter(const QString& value);
    Q_INVOKABLE void setBlockedFilter(int value);
    Q_INVOKABLE void setOverdueFilter(int value);
    Q_INVOKABLE void setOriginFilter(const QString& value);
    Q_INVOKABLE void setSelectedDate(const QString& isoDate);
    Q_INVOKABLE void sortList(const QString& field, bool descending);
    Q_INVOKABLE bool saveTask(const QVariantMap& values);
    Q_INVOKABLE bool removeTask(const QString& taskId);
    Q_INVOKABLE bool setTaskCompleted(const QString& taskId, bool completed);
    Q_INVOKABLE bool moveTask(const QString& taskId, const QString& status);
    Q_INVOKABLE bool shiftTaskOrder(const QString& taskId, int direction);
    Q_INVOKABLE bool createMilestone(const QString& title, const QString& dueDate);
    Q_INVOKABLE bool saveMilestone(const QVariantMap& values);
    Q_INVOKABLE bool removeMilestone(const QString& milestoneId);
    Q_INVOKABLE QVariantList timeEntries(const QString& taskId);
    Q_INVOKABLE bool addTimeEntry(const QString& taskId, const QString& minutes, const QString& note);
    Q_INVOKABLE bool removeTimeEntry(const QString& taskId, const QString& entryId);
    Q_INVOKABLE bool branchExists(const QString& branch) const;
    Q_INVOKABLE QVariantList branchOptionsForTask(const QString& taskId) const;
    Q_INVOKABLE void openGitBranch(const QString& branch);
    Q_INVOKABLE bool openGitHubUrl(const QString& url);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void refresh();
    void setSessionAllowed(bool allowed);
signals:
    void changed();
    void openGitBranchRequested(QString branch);
private slots:
    void workspaceChanged(const QString& workspaceId, const QString& absolutePath);
private:
    void loadWorkspace(const QString& workspaceId, const QString& name);
    void loadMilestones();
    void loadBranches();
    void scheduleFilterReload();
    void reloadAfterMutation();
    void applyProxyFilters();
    bool reorderStatus(const QString& status, const QString& movedToEnd = {});
    void setFailure(const QString& message);
    const domain::planner::PlannerTask* taskById(const QString& id) const;
    static std::optional<domain::planner::PlannerStatus> parseStatus(const QString& value);
    application::planner::PlannerService& m_service;
    application::settings::SettingsService& m_settings;
    application::workspaces::WorkspaceContext& m_workspaceContext;
    application::tasks::TaskScheduler* m_scheduler = nullptr;
    PlannerTaskModel m_tasks;
    mutable domain::planner::PlannerTask m_taskScratch;
    PlannerTaskFilterModel m_list;
    PlannerTaskFilterModel m_calendar;
    PlannerTaskFilterModel m_undated;
    std::array<PlannerTaskFilterModel*, 5> m_board{};
    QString m_workspaceId;
    QString m_workspaceName;
    QString m_view = QStringLiteral("board");
    QString m_selectedDate;
    QString m_error;
    QString m_searchText;
    QString m_statusFilter;
    QString m_priorityFilter;
    QString m_tagFilter;
    QString m_milestoneFilter;
    QString m_gitBranchFilter;
    QString m_originFilter;
    QString m_workspacePath;
    QString m_branchStatus;
    QStringList m_localBranches;
    QVariantList m_branchOptions;
    application::tasks::TaskHandle m_branchTask;
    QTimer m_filterReloadTimer;
    quint64 m_branchGeneration = 0;
    int m_completionFilter = -1;
    int m_blockedFilter = -1;
    int m_overdueFilter = -1;
    quint64 m_loadGeneration = 0;
    QVariantList m_milestones;
    bool m_collapsed = false;
    bool m_loading = false;
    bool m_sessionAllowed = true;
    bool m_branchLoading = false;
};

} // namespace devhub::presentation::overview::planner
