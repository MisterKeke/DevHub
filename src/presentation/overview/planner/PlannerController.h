#pragma once

#include "PlannerTaskModel.h"
#include "../../../application/planner/PlannerService.h"

#include <QObject>
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
public:
    PlannerController(application::planner::PlannerService& service,
                       application::settings::SettingsService& settings,
                       application::workspaces::WorkspaceContext& workspaceContext,
                       QObject* parent = nullptr);
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
    Q_INVOKABLE void setSelectedDate(const QString& isoDate);
    Q_INVOKABLE void sortList(const QString& field, bool descending);
    Q_INVOKABLE bool saveTask(const QVariantMap& values);
    Q_INVOKABLE bool removeTask(const QString& taskId);
    Q_INVOKABLE bool setTaskCompleted(const QString& taskId, bool completed);
    Q_INVOKABLE bool moveTask(const QString& taskId, const QString& status);
    Q_INVOKABLE bool shiftTaskOrder(const QString& taskId, int direction);
    Q_INVOKABLE bool createMilestone(const QString& title, const QString& dueDate);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void refresh();
    void setSessionAllowed(bool allowed);
signals:
    void changed();
private slots:
    void workspaceChanged(const QString& workspaceId, const QString& absolutePath);
private:
    void loadWorkspace(const QString& workspaceId, const QString& name);
    void loadMilestones();
    void reloadAfterMutation();
    void applyProxyFilters();
    bool reorderStatus(const QString& status, const QString& movedToEnd = {});
    void setFailure(const QString& message);
    const domain::planner::PlannerTask* taskById(const QString& id) const;
    static std::optional<domain::planner::PlannerStatus> parseStatus(const QString& value);
    application::planner::PlannerService& m_service;
    application::settings::SettingsService& m_settings;
    application::workspaces::WorkspaceContext& m_workspaceContext;
    PlannerTaskModel m_tasks;
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
    int m_completionFilter = -1;
    quint64 m_loadGeneration = 0;
    QVariantList m_milestones;
    bool m_collapsed = false;
    bool m_loading = false;
    bool m_sessionAllowed = true;
};

} // namespace devhub::presentation::overview::planner
