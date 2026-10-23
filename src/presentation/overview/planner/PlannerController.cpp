#include "PlannerController.h"

#include "../../../application/settings/SettingsService.h"
#include "../../../application/workspaces/WorkspaceContext.h"
#include "../../../application/git/GitExecutableLocator.h"
#include "../../../application/git/GitService.h"
#include "../../../domain/planner/PlannerRules.h"

#include <QDate>
#include <QDesktopServices>
#include <QQmlEngine>
#include <QUrl>
#include <QSet>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

namespace devhub::presentation::overview::planner {
using domain::planner::PlannerStatus;
using domain::planner::PlannerTask;
using domain::planner::PlannerTaskFilter;

namespace {
const std::array<PlannerStatus, 5> Statuses{PlannerStatus::Backlog, PlannerStatus::Todo,
    PlannerStatus::InProgress, PlannerStatus::InReview, PlannerStatus::Done};
QString statusName(PlannerStatus status) { return domain::planner::plannerStatusName(status); }
QDate parseDate(const QVariant& value)
{
    if (!value.isValid() || value.isNull() || value.toString().trimmed().isEmpty()) return {};
    return QDate::fromString(value.toString(), Qt::ISODate);
}
QStringList tagsFrom(const QVariant& value)
{
    QStringList tags;
    if (value.metaType().id() == QMetaType::QStringList) tags = value.toStringList();
    else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const auto& item : value.toList()) tags.append(item.toString());
    } else tags = value.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (auto& tag : tags) tag = tag.trimmed();
    tags.removeAll(QString{});
    return tags;
}
QStringList stringsFrom(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QStringList) return value.toStringList();
    QStringList result;
    if (value.metaType().id() == QMetaType::QVariantList) {
        for (const auto& item : value.toList()) result.append(item.toString());
    }
    return result;
}
QString errorText(const auto& result)
{ return result.error().userMessage.isEmpty() ? QObject::tr("The Planner change could not be saved.") : result.error().userMessage; }
}

PlannerController::PlannerController(application::planner::PlannerService& service,
    application::settings::SettingsService& settings,
    application::workspaces::WorkspaceContext& workspaceContext, QObject* parent,
    application::tasks::TaskScheduler* scheduler)
    : QObject(parent), m_service(service), m_settings(settings), m_workspaceContext(workspaceContext),
      m_scheduler(scheduler), m_tasks(), m_list(), m_calendar(), m_undated()
{
    // These models are value members (or QObject children) owned by this controller.
    // In particular, boardModel() returns a model through an invokable; Qt otherwise
    // may treat an unparented returned QObject as JavaScript-owned.
    QQmlEngine::setObjectOwnership(&m_tasks, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&m_list, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&m_calendar, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&m_undated, QQmlEngine::CppOwnership);
    m_list.setSourceModel(&m_tasks);
    m_calendar.setSourceModel(&m_tasks);
    m_undated.setSourceModel(&m_tasks);
    m_calendar.setDateFilter(QDate::currentDate().toString(Qt::ISODate));
    m_undated.setDateFilter({}, true);
    for (std::size_t i = 0; i < Statuses.size(); ++i) {
        m_board[i] = new PlannerTaskFilterModel(this);
        QQmlEngine::setObjectOwnership(m_board[i], QQmlEngine::CppOwnership);
        m_board[i]->setSourceModel(&m_tasks);
        m_board[i]->setStatusFilter(statusName(Statuses[i]));
    }
    connect(&m_workspaceContext, &application::workspaces::WorkspaceContext::workspaceChanged,
            this, &PlannerController::workspaceChanged);
    m_filterReloadTimer.setSingleShot(true);
    m_filterReloadTimer.setInterval(160);
    connect(&m_filterReloadTimer, &QTimer::timeout, this, &PlannerController::reloadAfterMutation);
    if (m_workspaceContext.activeWorkspace()) {
        loadWorkspace(m_workspaceContext.activeWorkspace()->id().toString(),
                      m_workspaceContext.activeWorkspace()->displayName());
        m_workspacePath = m_workspaceContext.activeWorkspace()->path().canonicalPath();
        loadBranches();
    }
}

std::optional<PlannerStatus> PlannerController::parseStatus(const QString& value)
{ return domain::planner::plannerStatusFromName(value); }

void PlannerController::workspaceChanged(const QString& id, const QString& absolutePath)
{
    m_branchTask.cancel();
    m_localBranches.clear();
    m_branchOptions.clear();
    m_branchStatus.clear();
    m_workspacePath = absolutePath;
    const auto active = m_workspaceContext.activeWorkspace();
    loadWorkspace(id, id.isEmpty() || !active ? QString{} : active->displayName());
    loadBranches();
}

void PlannerController::loadWorkspace(const QString& id, const QString& name)
{
    m_filterReloadTimer.stop();
    ++m_loadGeneration;
    m_tasks.clear();
    m_milestones.clear();
    m_error.clear();
    m_workspaceId = m_sessionAllowed ? id : QString{};
    m_workspaceName = m_sessionAllowed ? name : QString{};
    m_view = m_workspaceId.isEmpty() ? QStringLiteral("board")
        : m_settings.value(QStringLiteral("planner/view"), m_workspaceId).toString();
    if (m_view != QStringLiteral("board") && m_view != QStringLiteral("list") && m_view != QStringLiteral("calendar"))
        m_view = QStringLiteral("board");
    m_collapsed = !m_workspaceId.isEmpty() && m_settings.value(QStringLiteral("planner/collapsed"), m_workspaceId).toBool();
    if (!m_workspaceId.isEmpty()) reloadAfterMutation();
    emit changed();
}

void PlannerController::reloadAfterMutation()
{
    m_filterReloadTimer.stop();
    if (m_workspaceId.isEmpty() || !m_sessionAllowed) { m_tasks.clear(); emit changed(); return; }
    const QString workspace = m_workspaceId;
    const auto generation = m_loadGeneration;
    m_loading = true;
    emit changed();
    QVector<PlannerTask> all;
    PlannerTaskFilter filter;
    filter.searchText = m_searchText.trimmed();
    if (!m_statusFilter.isEmpty()) filter.status = parseStatus(m_statusFilter);
    if (!m_priorityFilter.isEmpty()) filter.priority = m_priorityFilter;
    if (m_completionFilter >= 0) filter.completed = m_completionFilter == 1;
    filter.milestoneId = m_milestoneFilter;
    filter.gitBranch = m_gitBranchFilter;
    filter.sourceType = m_originFilter;
    if (m_blockedFilter >= 0) filter.blocked = m_blockedFilter == 1;
    if (m_overdueFilter >= 0) filter.overdue = m_overdueFilter == 1;
    filter.tag = m_tagFilter.trimmed().toCaseFolded();
    filter.sort = domain::planner::PlannerSortField::BoardPosition;
    filter.limit = 200;
    int offset = 0;
    while (true) {
        filter.offset = offset;
        auto page = m_service.listTasks(workspace, filter);
        if (!page) { setFailure(errorText(page)); m_loading = false; emit changed(); return; }
        const int received = static_cast<int>(page.value().size());
        all += page.value();
        if (received < filter.limit) break;
        offset += received;
    }
    if (generation != m_loadGeneration || workspace != m_workspaceId) return;
    m_tasks.setTasks(std::move(all));
    loadMilestones();
    m_loading = false;
    m_error.clear();
    emit changed();
}

void PlannerController::loadMilestones()
{
    m_milestones.clear();
    if (m_workspaceId.isEmpty()) return;
    const auto result = m_service.listMilestones(m_workspaceId);
    if (!result) { setFailure(errorText(result)); return; }
    for (const auto& milestone : result.value()) {
        const int taskCount = milestone.taskCount;
        const int completedCount = milestone.completedTaskCount;
        m_milestones.append(QVariantMap{
            {QStringLiteral("id"), milestone.id}, {QStringLiteral("title"), milestone.title},
            {QStringLiteral("description"), milestone.description},
            {QStringLiteral("startDate"), milestone.startDate.isValid() ? milestone.startDate.toString(Qt::ISODate) : QString{}},
            {QStringLiteral("dueDate"), milestone.dueDate.isValid() ? milestone.dueDate.toString(Qt::ISODate) : QString{}},
            {QStringLiteral("completed"), milestone.completed}, {QStringLiteral("revision"), milestone.revision},
            {QStringLiteral("taskCount"), taskCount}, {QStringLiteral("completedTaskCount"), completedCount},
            {QStringLiteral("progress"), taskCount == 0 ? 0 : completedCount * 100 / taskCount}});
    }
}

void PlannerController::loadBranches()
{
    m_branchTask.cancel();
    const quint64 generation = ++m_branchGeneration;
    m_localBranches.clear();
    m_branchOptions.clear();
    m_branchLoading = false;
    m_branchStatus.clear();
    if (m_workspaceId.isEmpty() || m_workspacePath.isEmpty()) { emit changed(); return; }
    if (!m_scheduler) {
        m_branchStatus = tr("Git branch lookup is unavailable.");
        emit changed();
        return;
    }
    const QString path = m_workspacePath;
    const QString workspace = m_workspaceId;
    application::tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("planner-git-branches");
    const QUuid workspaceUuid(workspace);
    if (!workspaceUuid.isNull()) request.workspaceId = workspaceUuid;
    request.generation = generation;
    m_branchLoading = true;
    m_branchStatus = tr("Reading local Git branches…");
    emit changed();
    m_branchTask = m_scheduler->submit<QStringList>(request,
        [path](const application::tasks::TaskContext& context) {
            application::git::GitExecutableLocator locator;
            auto executable = locator.locate();
            if (!executable)
                return shared::common::Result<QStringList>::failure(executable.error());
            application::git::GitService service{
                application::git::GitCommandRunner(executable.value().path)};
            auto branches = service.branches(path, false, context.cancellationToken);
            if (!branches)
                return shared::common::Result<QStringList>::failure(branches.error());
            QStringList names;
            for (const auto& branch : branches.value()) names.append(branch.name);
            names.removeDuplicates();
            names.sort(Qt::CaseInsensitive);
            return shared::common::Result<QStringList>::success(std::move(names));
        }, [this, generation, workspace](shared::common::TaskOutcome<QStringList> outcome) {
            if (generation != m_branchGeneration || workspace != m_workspaceId) return;
            m_branchLoading = false;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                m_branchStatus = tr("Branch lookup cancelled.");
                emit changed();
                return;
            }
            auto result = std::get<shared::common::Result<QStringList>>(std::move(outcome));
            if (!result) {
                m_branchStatus = result.error().userMessage.isEmpty()
                    ? tr("Could not read Git branches.") : result.error().userMessage;
            } else {
                m_localBranches = std::move(result).value();
                for (const QString& branch : m_localBranches)
                    m_branchOptions.append(QVariantMap{{QStringLiteral("name"), branch},
                        {QStringLiteral("displayName"), branch}, {QStringLiteral("exists"), true}});
                m_branchStatus = m_localBranches.isEmpty()
                    ? tr("No local branches found.") : tr("%1 local branch(es) available.").arg(m_localBranches.size());
            }
            emit changed();
        });
}

void PlannerController::setSessionAllowed(bool allowed)
{
    if (m_sessionAllowed == allowed) return;
    m_sessionAllowed = allowed;
    ++m_loadGeneration;
    if (!allowed) {
        m_branchTask.cancel();
        ++m_branchGeneration;
        m_localBranches.clear(); m_branchOptions.clear(); m_branchStatus.clear(); m_branchLoading = false;
        m_tasks.clear(); m_workspaceId.clear(); m_workspaceName.clear(); m_milestones.clear();
        m_loading = false; m_error.clear(); emit changed(); return;
    }
    const auto active = m_workspaceContext.activeWorkspace();
    if (active) m_workspacePath = active->path().canonicalPath();
    loadWorkspace(active ? active->id().toString() : QString{}, active ? active->displayName() : QString{});
    loadBranches();
}

int PlannerController::totalCount() const { return static_cast<int>(m_tasks.tasks().size()); }
int PlannerController::inProgressCount() const
{ return static_cast<int>(std::count_if(m_tasks.tasks().cbegin(), m_tasks.tasks().cend(), [](const auto& task) { return task.status == PlannerStatus::InProgress; })); }
int PlannerController::completedCount() const
{ return static_cast<int>(std::count_if(m_tasks.tasks().cbegin(), m_tasks.tasks().cend(), [](const auto& task) { return task.completed; })); }
int PlannerController::overdueCount() const
{ return static_cast<int>(std::count_if(m_tasks.tasks().cbegin(), m_tasks.tasks().cend(), [](const auto& task) { return !task.completed && task.dueDate.isValid() && task.dueDate < QDate::currentDate(); })); }
int PlannerController::progressPercent() const { return totalCount() == 0 ? 0 : (completedCount() * 100 / totalCount()); }
QString PlannerController::nearestDeadline() const
{
    QDate nearest;
    for (const auto& task : m_tasks.tasks()) if (!task.completed && task.dueDate.isValid()
        && task.dueDate >= QDate::currentDate() && (!nearest.isValid() || task.dueDate < nearest)) nearest = task.dueDate;
    return nearest.isValid() ? nearest.toString(QStringLiteral("d MMM yyyy")) : tr("No upcoming deadline");
}

QAbstractItemModel* PlannerController::boardModel(const QString& status) const
{
    const auto parsed = parseStatus(status);
    if (!parsed) return nullptr;
    const auto index = std::find(Statuses.cbegin(), Statuses.cend(), *parsed);
    return index == Statuses.cend() ? nullptr : m_board[static_cast<std::size_t>(std::distance(Statuses.cbegin(), index))];
}
int PlannerController::statusCount(const QString& status) const
{
    const auto parsed = parseStatus(status); if (!parsed) return 0;
    return static_cast<int>(std::count_if(m_tasks.tasks().cbegin(), m_tasks.tasks().cend(), [parsed](const auto& task) { return task.status == *parsed; }));
}

void PlannerController::applyProxyFilters()
{
    auto apply = [&](PlannerTaskFilterModel* model) {
        model->setStatusFilter(m_statusFilter); model->setSearchText(m_searchText); model->setPriorityFilter(m_priorityFilter);
        model->setTagFilter(m_tagFilter); model->setCompletionFilter(m_completionFilter);
    };
    apply(&m_list); apply(&m_calendar); apply(&m_undated);
    for (auto* model : m_board) {
        model->setSearchText(m_searchText); model->setPriorityFilter(m_priorityFilter);
        model->setTagFilter(m_tagFilter); model->setCompletionFilter(m_completionFilter);
    }
    m_calendar.setDateFilter(m_selectedDate);
    m_undated.setDateFilter({}, true);
}

void PlannerController::scheduleFilterReload()
{
    if (!m_workspaceId.isEmpty() && m_sessionAllowed) m_filterReloadTimer.start();
}

void PlannerController::setView(const QString& value)
{
    if (value != QStringLiteral("board") && value != QStringLiteral("list") && value != QStringLiteral("calendar")) return;
    m_view = value;
    if (!m_workspaceId.isEmpty()) {
        const auto result = m_settings.setValue(QStringLiteral("planner/view"), value, m_workspaceId);
        if (!result) setFailure(errorText(result));
    }
    emit changed();
}
void PlannerController::setCollapsed(bool value)
{
    m_collapsed = value;
    if (!m_workspaceId.isEmpty()) {
        const auto result = m_settings.setValue(QStringLiteral("planner/collapsed"), value, m_workspaceId);
        if (!result) setFailure(errorText(result));
    }
    emit changed();
}
void PlannerController::setSearchText(const QString& value) { m_searchText = value; applyProxyFilters(); scheduleFilterReload(); }
void PlannerController::setStatusFilter(const QString& value) { m_statusFilter = value; applyProxyFilters(); scheduleFilterReload(); }
void PlannerController::setPriorityFilter(const QString& value) { m_priorityFilter = value; applyProxyFilters(); scheduleFilterReload(); }
void PlannerController::setCompletionFilter(int value) { m_completionFilter = value; applyProxyFilters(); scheduleFilterReload(); }
void PlannerController::setTagFilter(const QString& value) { m_tagFilter = value; applyProxyFilters(); scheduleFilterReload(); }
void PlannerController::setMilestoneFilter(const QString& value) { m_milestoneFilter = value; scheduleFilterReload(); }
void PlannerController::setGitBranchFilter(const QString& value) { m_gitBranchFilter = value; scheduleFilterReload(); }
void PlannerController::setBlockedFilter(int value) { m_blockedFilter = value; scheduleFilterReload(); }
void PlannerController::setOverdueFilter(int value) { m_overdueFilter = value; scheduleFilterReload(); }
void PlannerController::setOriginFilter(const QString& value) { m_originFilter = value; scheduleFilterReload(); }
void PlannerController::setSelectedDate(const QString& value)
{
    const auto date = QDate::fromString(value, Qt::ISODate);
    if (!date.isValid()) return;
    m_selectedDate = date.toString(Qt::ISODate); m_calendar.setDateFilter(m_selectedDate); emit changed();
}
void PlannerController::sortList(const QString& field, bool descending)
{
    const QHash<QString, int> roles{{QStringLiteral("title"),PlannerTaskModel::TitleRole},
        {QStringLiteral("priority"),PlannerTaskModel::PriorityRole}, {QStringLiteral("dueDate"),PlannerTaskModel::DueDateRole},
        {QStringLiteral("status"),PlannerTaskModel::StatusRole}, {QStringLiteral("difficulty"),PlannerTaskModel::DifficultyRole},
        {QStringLiteral("createdAt"),PlannerTaskModel::CreatedAtRole},
        {QStringLiteral("completed"),PlannerTaskModel::CompletedRole}};
    if (roles.contains(field)) m_list.setSortField(roles.value(field), descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

QVariantMap PlannerController::taskDetails(const QString& id) const
{
    const int row = [&] { for (int i = 0; i < m_tasks.rowCount(); ++i) if (m_tasks.index(i,0).data(PlannerTaskModel::IdRole).toString() == id) return i; return -1; }();
    const auto* task = taskById(id);
    if (!task) return {};
    PlannerTaskModel detached;
    const PlannerTaskModel* model = &m_tasks;
    int modelRow = row;
    if (row < 0) {
        detached.setTasks({*task});
        model = &detached;
        modelRow = 0;
    }
    const auto index = model->index(modelRow, 0);
    QVariantMap values;
    const auto roleNames = model->roleNames();
    for (auto it = roleNames.cbegin(); it != roleNames.cend(); ++it)
        values.insert(QString::fromUtf8(it.value()), index.data(it.key()));
    values.insert(QStringLiteral("workspaceId"), task->workspaceId);
    values.insert(QStringLiteral("gitBranchExists"), branchExists(values.value(QStringLiteral("gitBranch")).toString()));
    values.insert(QStringLiteral("serviceName"), QString{});
    values.insert(QStringLiteral("linkedServiceAvailable"), false);
    for (const QVariant& option : m_serviceOptions) {
        const QVariantMap service = option.toMap();
        if (service.value(QStringLiteral("id")).toString() == task->linkedServiceId) {
            values.insert(QStringLiteral("serviceName"), service.value(QStringLiteral("name")).toString());
            values.insert(QStringLiteral("linkedServiceAvailable"), hasWorkspace()
                && task->workspaceId == m_workspaceId);
            break;
        }
    }
    values.insert(QStringLiteral("actionRuns"), actionRuns(id));
    return values;
}

void PlannerController::setServiceOptions(QVariantList options)
{
    QVariantList safe;
    QSet<QString> ids;
    for (const QVariant& value : options) {
        const QVariantMap row = value.toMap();
        const QString id = row.value(QStringLiteral("id")).toString().trimmed();
        const QString name = row.value(QStringLiteral("name")).toString().trimmed();
        if (id.isEmpty() || QUuid::fromString(id).isNull() || name.isEmpty() || ids.contains(id)) continue;
        ids.insert(id);
        safe.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}});
        if (safe.size() >= 100) break;
    }
    m_serviceOptions = std::move(safe);
    emit changed();
}

QVariantList PlannerController::actionRuns(const QString& taskId) const
{
    if (!hasWorkspace() || taskId.isEmpty()) return {};
    const auto runs = m_service.listActionRuns(m_workspaceId, taskId, 20);
    if (!runs) return {};
    QVariantList rows;
    for (const auto& run : runs.value()) {
        const QString status = !run.finishedAtUtc.isValid() ? tr("Starting")
            : run.succeeded.value_or(false) ? tr("Started") : tr("Failed");
        rows.append(QVariantMap{{QStringLiteral("id"), run.id},
            {QStringLiteral("actionId"), run.actionId}, {QStringLiteral("status"), status},
            {QStringLiteral("succeeded"), run.succeeded.value_or(false)},
            {QStringLiteral("finished"), run.finishedAtUtc.isValid()},
            {QStringLiteral("startedAt"), run.startedAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
            {QStringLiteral("durationMs"), run.durationMilliseconds},
            {QStringLiteral("errorSummary"), run.errorSummary},
            {QStringLiteral("logReference"), run.logReference}});
    }
    return rows;
}

void PlannerController::openWorkspaceFolder()
{ if (hasWorkspace()) emit openWorkspaceFolderRequested(m_workspaceId); }
void PlannerController::openWorkspaceNotes()
{ if (hasWorkspace()) emit openWorkspaceNotesRequested(m_workspaceId); }
void PlannerController::openGitModule()
{ if (hasWorkspace()) emit openGitModuleRequested(m_workspaceId); }

void PlannerController::openLinkedService(const QString& taskId)
{
    const auto* task = hasWorkspace() ? taskById(taskId) : nullptr;
    if (!task || task->workspaceId != m_workspaceId || task->linkedServiceId.isEmpty()) return;
    for (const QVariant& value : m_serviceOptions)
        if (value.toMap().value(QStringLiteral("id")).toString() == task->linkedServiceId) {
            emit openLinkedServiceRequested(m_workspaceId, task->linkedServiceId); return;
        }
    setFailure(tr("The linked service is unavailable in this workspace. Choose a configured service in task details."));
}

void PlannerController::openLinkedServiceLogs(const QString& taskId)
{
    const auto* task = hasWorkspace() ? taskById(taskId) : nullptr;
    if (!task || task->workspaceId != m_workspaceId || task->linkedServiceId.isEmpty()) return;
    for (const QVariant& value : m_serviceOptions)
        if (value.toMap().value(QStringLiteral("id")).toString() == task->linkedServiceId) {
            emit openLinkedServiceLogsRequested(m_workspaceId, task->linkedServiceId); return;
        }
    setFailure(tr("The linked service is unavailable in this workspace."));
}

void PlannerController::startLinkedService(const QString& taskId)
{
    const auto* task = hasWorkspace() ? taskById(taskId) : nullptr;
    if (!task || task->workspaceId != m_workspaceId || task->linkedServiceId.isEmpty()) {
        setFailure(tr("This task has no available configured service.")); return;
    }
    const QString serviceId = task->linkedServiceId;
    const bool configured = std::any_of(m_serviceOptions.cbegin(), m_serviceOptions.cend(), [&serviceId](const QVariant& value) {
        return value.toMap().value(QStringLiteral("id")).toString() == serviceId;
    });
    if (!configured) { setFailure(tr("The linked service is unavailable in this workspace.")); return; }
    emit startLinkedServiceRequested(m_workspaceId, taskId, serviceId);
}

void PlannerController::cancelLinkedServiceStart(const QString& taskId)
{
    const auto* task = hasWorkspace() ? taskById(taskId) : nullptr;
    if (task && task->workspaceId == m_workspaceId && !task->linkedServiceId.isEmpty())
        emit cancelLinkedServiceStartRequested(m_workspaceId, taskId, task->linkedServiceId);
}

std::optional<domain::planner::PlannerActionRun> PlannerController::beginLinkedServiceAction(
    const QString& taskId, const QString& serviceId, const QString& logReference)
{
    if (!hasWorkspace()) { setFailure(tr("Sign in and select a workspace before starting a Planner service.")); return std::nullopt; }
    const auto* task = taskById(taskId);
    if (!task || task->workspaceId != m_workspaceId || task->linkedServiceId != serviceId) {
        setFailure(tr("The task or linked service changed. Reload task details and try again.")); return std::nullopt;
    }
    const bool configured = std::any_of(m_serviceOptions.cbegin(), m_serviceOptions.cend(), [&serviceId](const QVariant& value) {
        return value.toMap().value(QStringLiteral("id")).toString() == serviceId;
    });
    if (!configured) { setFailure(tr("The linked service is unavailable in this workspace.")); return std::nullopt; }
    auto result = m_service.beginActionRun(m_workspaceId, taskId,
        QStringLiteral("service.start:%1").arg(serviceId), logReference);
    if (!result) { setFailure(errorText(result)); return std::nullopt; }
    return std::move(result).value();
}

bool PlannerController::finishActionRun(const domain::planner::PlannerActionRun& run, bool succeeded,
                                        const QString& errorSummary)
{
    const auto result = m_service.finishActionRun(run.workspaceId, run.taskId, run.id, succeeded, errorSummary);
    if (!result) { setFailure(errorText(result)); return false; }
    emit actionRunsChanged(run.taskId);
    if (run.workspaceId == m_workspaceId) emit changed();
    return true;
}

void PlannerController::requestCreateTask()
{ if (hasWorkspace()) emit createTaskRequested(); }
void PlannerController::requestFocusSearch()
{ if (hasWorkspace()) emit focusSearchRequested(); }
void PlannerController::requestOpenTask(const QString& taskId)
{ if (hasWorkspace() && taskById(taskId)) emit openTaskDetailsRequested(taskId); }

bool PlannerController::branchExists(const QString& branch) const
{
    return !branch.isEmpty() && m_localBranches.contains(branch);
}

QVariantList PlannerController::branchOptionsForTask(const QString& taskId) const
{
    QVariantList options{QVariantMap{{QStringLiteral("name"), QString{}},
        {QStringLiteral("displayName"), tr("No branch")}, {QStringLiteral("exists"), true}}};
    for (const QString& branch : m_localBranches)
        options.append(QVariantMap{{QStringLiteral("name"), branch},
            {QStringLiteral("displayName"), branch}, {QStringLiteral("exists"), true}});
    const auto* task = taskById(taskId);
    if (task && !task->gitBranch.isEmpty() && !m_localBranches.contains(task->gitBranch))
        options.append(QVariantMap{{QStringLiteral("name"), task->gitBranch},
            {QStringLiteral("displayName"), tr("%1 (missing)").arg(task->gitBranch)},
            {QStringLiteral("exists"), false}});
    return options;
}

void PlannerController::openGitBranch(const QString& branch)
{
    if (branch.trimmed().isEmpty()) return;
    if (!branchExists(branch))
        m_branchStatus = tr("Branch ‘%1’ is not present in this workspace.").arg(branch);
    emit openGitBranchRequested(branch);
    emit changed();
}

bool PlannerController::openGitHubUrl(const QString& url)
{
    const auto valid = m_service.validateGitHubUrl(url.trimmed());
    if (!valid) { setFailure(errorText(valid)); return false; }
    if (url.trimmed().isEmpty()) return false;
    if (!QDesktopServices::openUrl(QUrl(url.trimmed(), QUrl::StrictMode))) {
        setFailure(tr("DevHub could not open this GitHub link."));
        return false;
    }
    m_error.clear();
    emit changed();
    return true;
}

QVariantList PlannerController::dependencyChoices(const QString& exclude) const
{
    QVariantList choices;
    if (m_workspaceId.isEmpty()) return choices;
    PlannerTaskFilter filter;
    filter.limit = 200;
    int offset = 0;
    while (true) {
        filter.offset = offset;
        const auto page = m_service.listTasks(m_workspaceId, filter);
        if (!page) return {};
        for (const auto& task : page.value()) if (task.id != exclude)
            choices.append(QVariantMap{{QStringLiteral("taskId"), task.id},
                {QStringLiteral("title"), task.completed ? tr("%1 (complete)").arg(task.title) : task.title}});
        const int received = static_cast<int>(page.value().size());
        if (received < filter.limit) break;
        offset += received;
    }
    return choices;
}

int PlannerController::countForDate(const QString& isoDate) const
{
    if (!QDate::fromString(isoDate, Qt::ISODate).isValid()) return 0;
    int count = 0;
    for (const auto& task : m_tasks.tasks()) {
        if (task.dueDate.toString(Qt::ISODate) != isoDate) continue;
        if (!m_statusFilter.isEmpty() && statusName(task.status) != m_statusFilter) continue;
        if (!m_priorityFilter.isEmpty() && task.priority != m_priorityFilter) continue;
        if (m_completionFilter >= 0 && task.completed != (m_completionFilter == 1)) continue;
        if (!m_tagFilter.isEmpty() && std::none_of(task.tags.cbegin(), task.tags.cend(), [this](const auto& tag) { return tag.toCaseFolded().contains(m_tagFilter.trimmed().toCaseFolded()); })) continue;
        if (!m_searchText.trimmed().isEmpty()) {
            const auto search = m_searchText.trimmed().toCaseFolded();
            bool found = task.title.toCaseFolded().contains(search) || task.description.toCaseFolded().contains(search)
                || task.gitBranch.toCaseFolded().contains(search);
            for (const auto& tag : task.tags) found = found || tag.toCaseFolded().contains(search);
            if (!found) continue;
        }
        ++count;
    }
    return count;
}

const PlannerTask* PlannerController::taskById(const QString& id) const
{
    if (const auto* loaded = m_tasks.taskById(id)) return loaded;
    if (m_workspaceId.isEmpty() || id.isEmpty()) return nullptr;
    auto task = m_service.getTask(m_workspaceId, id);
    if (!task) return nullptr;
    m_taskScratch = std::move(task).value();
    return &m_taskScratch;
}
void PlannerController::setFailure(const QString& message) { m_error = message; emit changed(); }
void PlannerController::clearError() { if (!m_error.isEmpty()) { m_error.clear(); emit changed(); } }
void PlannerController::refresh() { reloadAfterMutation(); }

bool PlannerController::saveTask(const QVariantMap& values)
{
    if (!hasWorkspace()) { setFailure(tr("Select a workspace before editing Planner tasks.")); return false; }
    const QString id = values.value(QStringLiteral("taskId")).toString();
    const PlannerTask* existing = id.isEmpty() ? nullptr : taskById(id);
    if (!id.isEmpty() && !existing) { setFailure(tr("This task is no longer available. Reload the workspace and try again.")); return false; }
    PlannerTask draft = existing ? *existing : PlannerTask{};
    draft.title = values.value(QStringLiteral("title")).toString().trimmed();
    draft.description = values.value(QStringLiteral("description")).toString();
    draft.priority = values.value(QStringLiteral("priority"), QStringLiteral("medium")).toString();
    draft.difficulty = values.value(QStringLiteral("difficulty")).toString();
    draft.dueDate = parseDate(values.value(QStringLiteral("dueDate")));
    draft.startDate = parseDate(values.value(QStringLiteral("startDate")));
    if ((!values.value(QStringLiteral("dueDate")).toString().trimmed().isEmpty() && !draft.dueDate.isValid())
        || (!values.value(QStringLiteral("startDate")).toString().trimmed().isEmpty() && !draft.startDate.isValid())) {
        setFailure(tr("Enter start and due dates as valid YYYY-MM-DD calendar dates.")); return false;
    }
    const QVariant estimated = values.value(QStringLiteral("estimatedMinutes"));
    if (!estimated.isValid() || estimated.isNull() || estimated.toString().trimmed().isEmpty()) draft.estimatedMinutes.reset();
    else { bool ok = false; const qint64 n = estimated.toLongLong(&ok); if (!ok || n < 0) { setFailure(tr("Estimated duration must be a non-negative number of minutes.")); return false; } draft.estimatedMinutes = n; }
    if (values.contains(QStringLiteral("actualTrackedMinutes"))) {
        bool trackedOk = false;
        const qint64 tracked = values.value(QStringLiteral("actualTrackedMinutes")).toLongLong(&trackedOk);
        if (!trackedOk || tracked < 0) { setFailure(tr("Tracked time must be a non-negative number of minutes.")); return false; }
        draft.actualTrackedMinutes = tracked;
    }
    draft.milestoneId = values.value(QStringLiteral("milestoneId")).toString();
    const QString linkedServiceId = values.value(QStringLiteral("linkedServiceId")).toString().trimmed();
    const bool serviceConfigured = std::any_of(m_serviceOptions.cbegin(), m_serviceOptions.cend(), [&linkedServiceId](const QVariant& value) {
        return value.toMap().value(QStringLiteral("id")).toString() == linkedServiceId;
    });
    if (!linkedServiceId.isEmpty() && !serviceConfigured
        && (!existing || existing->linkedServiceId != linkedServiceId)) {
        setFailure(tr("Choose a service configured in the active workspace.")); return false;
    }
    draft.linkedServiceId = linkedServiceId;
    const QString requestedBranch = values.value(QStringLiteral("gitBranch")).toString().trimmed();
    if (!requestedBranch.isEmpty() && requestedBranch != draft.gitBranch && !branchExists(requestedBranch)) {
        setFailure(tr("Choose an existing local branch in this workspace."));
        return false;
    }
    draft.gitBranch = requestedBranch;
    draft.githubIssueUrl = values.value(QStringLiteral("githubIssueUrl")).toString().trimmed();
    draft.githubPullRequestUrl = values.value(QStringLiteral("githubPullRequestUrl")).toString().trimmed();
    draft.notes = values.value(QStringLiteral("notes")).toString();
    draft.blockedReason = values.value(QStringLiteral("blockedReason")).toString();
    const auto requestedStatus = parseStatus(values.value(QStringLiteral("status"), statusName(draft.status)).toString());
    if (!requestedStatus) { setFailure(tr("Choose a valid Kanban status.")); return false; }
    draft.status = *requestedStatus;
    draft = domain::planner::PlannerRules::withCompletion(std::move(draft), values.value(QStringLiteral("completed")).toBool());
    if (!draft.completed && draft.status == PlannerStatus::Done) draft = domain::planner::PlannerRules::withCompletion(std::move(draft), false);
    draft.tags = tagsFrom(values.value(QStringLiteral("tags")));
    const QVariantList subtaskDrafts = values.value(QStringLiteral("subtasks")).toList();
    if (!existing) {
        for (int i = 0; i < subtaskDrafts.size(); ++i) {
            const auto row = subtaskDrafts.at(i).toMap();
            if (!row.value(QStringLiteral("title")).toString().trimmed().isEmpty())
                draft.subtasks.append({{}, {}, row.value(QStringLiteral("title")).toString().trimmed(), row.value(QStringLiteral("completed")).toBool(), i});
        }
        draft.dependencyIds.clear();
        auto created = m_service.createTask(m_workspaceId, draft);
        if (!created) { setFailure(errorText(created)); return false; }
        PlannerTask saved = std::move(created).value();
        if (draft.status != PlannerStatus::Todo) {
            auto moved = m_service.moveTask(m_workspaceId, saved.id, saved.revision, draft.status);
            if (!moved) { setFailure(errorText(moved)); reloadAfterMutation(); return false; }
            saved = std::move(moved).value();
        }
        for (const auto& depValue : stringsFrom(values.value(QStringLiteral("dependencyIds")))) {
            auto linked = m_service.addDependency(m_workspaceId, saved.id, depValue, saved.revision);
            if (!linked) { setFailure(errorText(linked)); reloadAfterMutation(); return false; }
            ++saved.revision;
        }
    } else {
        const auto oldTags = existing->tags;
        const auto oldSubtasks = existing->subtasks;
        const auto oldDependencies = existing->dependencyIds;
        const qint64 expected = existing->revision;
        auto updated = m_service.updateTask(draft, expected);
        if (!updated) { setFailure(errorText(updated)); reloadAfterMutation(); return false; }
        PlannerTask saved = std::move(updated).value();
        qint64 revision = saved.revision;
        if (oldTags != draft.tags) {
            auto changed = m_service.setTags(m_workspaceId, saved.id, draft.tags, revision);
            if (!changed) { setFailure(errorText(changed)); reloadAfterMutation(); return false; }
            ++revision;
        }
        QHash<QString, domain::planner::PlannerSubtask> oldById;
        for (const auto& item : oldSubtasks) oldById.insert(item.id, item);
        QStringList desiredIds;
        for (int i = 0; i < subtaskDrafts.size(); ++i) {
            const auto row = subtaskDrafts.at(i).toMap();
            const QString title = row.value(QStringLiteral("title")).toString().trimmed();
            if (title.isEmpty()) continue;
            const QString subtaskId = row.value(QStringLiteral("id")).toString();
            if (oldById.contains(subtaskId)) {
                auto item = oldById.take(subtaskId);
                item.title = title; item.completed = row.value(QStringLiteral("completed")).toBool(); item.position = i;
                desiredIds.append(item.id);
                const auto old = std::find_if(oldSubtasks.cbegin(), oldSubtasks.cend(), [&subtaskId](const auto& value) { return value.id == subtaskId; });
                if (old != oldSubtasks.cend() && (old->title != item.title || old->completed != item.completed || old->position != item.position)) {
                    auto result = m_service.updateSubtask(m_workspaceId, saved.id, item, revision);
                    if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
                    ++revision;
                }
            } else {
                auto result = m_service.addSubtask(m_workspaceId, saved.id, title, i, revision);
                if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
                desiredIds.append(result.value().id); ++revision;
            }
        }
        for (auto it = oldById.cbegin(); it != oldById.cend(); ++it) {
            auto result = m_service.removeSubtask(m_workspaceId, saved.id, it.key(), revision);
            if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
            ++revision;
        }
        QStringList oldOrder; for (const auto& item : oldSubtasks) if (!oldById.contains(item.id)) oldOrder.append(item.id);
        if (oldOrder != desiredIds && desiredIds.size() > 1) {
            auto result = m_service.reorderSubtasks(m_workspaceId, saved.id, desiredIds, revision);
            if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
            ++revision;
        }
        const QStringList dependencies = stringsFrom(values.value(QStringLiteral("dependencyIds")));
        for (const auto& old : oldDependencies) if (!dependencies.contains(old)) {
            auto result = m_service.removeDependency(m_workspaceId, saved.id, old, revision);
            if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
            ++revision;
        }
        for (const auto& next : dependencies) if (!oldDependencies.contains(next)) {
            auto result = m_service.addDependency(m_workspaceId, saved.id, next, revision);
            if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
            ++revision;
        }
    }
    m_error.clear(); reloadAfterMutation(); return true;
}

bool PlannerController::removeTask(const QString& id)
{
    const auto* task = taskById(id);
    if (!task || !hasWorkspace()) return false;
    auto result = m_service.removeTask(m_workspaceId, id, task->revision);
    if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
    m_error.clear(); reloadAfterMutation(); return true;
}

bool PlannerController::setTaskCompleted(const QString& id, bool completed)
{
    const auto* task = taskById(id);
    if (!task || !hasWorkspace()) return false;
    auto result = m_service.setCompleted(m_workspaceId, id, task->revision, completed);
    if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
    m_error.clear(); reloadAfterMutation(); return true;
}

bool PlannerController::reorderStatus(const QString& status, const QString& movedToEnd)
{
    const auto parsed = parseStatus(status); if (!parsed) return false;
    QVector<const PlannerTask*> group;
    for (const auto& task : m_tasks.tasks()) if (task.status == *parsed && task.id != movedToEnd) group.append(&task);
    std::sort(group.begin(), group.end(), [](const auto* a, const auto* b) { return a->boardPosition == b->boardPosition ? a->createdAtUtc < b->createdAtUtc : a->boardPosition < b->boardPosition; });
    QStringList ids; QVector<qint64> revisions;
    for (const auto* task : group) { ids.append(task->id); revisions.append(task->revision); }
    if (!movedToEnd.isEmpty()) {
        const auto* moved = taskById(movedToEnd); if (!moved || moved->status != *parsed) return false;
        ids.append(moved->id); revisions.append(moved->revision);
    }
    auto result = m_service.reorderTasks(m_workspaceId, ids, revisions, *parsed);
    if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
    reloadAfterMutation(); return true;
}

bool PlannerController::moveTask(const QString& id, const QString& status)
{
    const auto* task = taskById(id); const auto parsed = parseStatus(status);
    if (!task || !parsed || !hasWorkspace()) return false;
    if (task->status == *parsed) return true;
    auto moved = m_service.moveTask(m_workspaceId, id, task->revision, *parsed);
    if (!moved) { setFailure(errorText(moved)); reloadAfterMutation(); return false; }
    reloadAfterMutation(); return reorderStatus(statusName(*parsed), id);
}

bool PlannerController::shiftTaskOrder(const QString& id, int direction)
{
    const auto* task = taskById(id); if (!task || !hasWorkspace() || direction == 0) return false;
    QVector<const PlannerTask*> group;
    for (const auto& item : m_tasks.tasks()) if (item.status == task->status) group.append(&item);
    std::sort(group.begin(), group.end(), [](const auto* a, const auto* b) { return a->boardPosition < b->boardPosition; });
    const auto it = std::find_if(group.begin(), group.end(), [&id](const auto* item) { return item->id == id; });
    if (it == group.end()) return false;
    const int index = static_cast<int>(std::distance(group.begin(), it)), next = index + (direction < 0 ? -1 : 1);
    if (next < 0 || next >= group.size()) return false;
    std::swap(group[index], group[next]);
    QStringList ids; QVector<qint64> revisions;
    for (const auto* item : group) { ids.append(item->id); revisions.append(item->revision); }
    auto result = m_service.reorderTasks(m_workspaceId, ids, revisions, task->status);
    if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
    m_error.clear(); reloadAfterMutation(); return true;
}

bool PlannerController::createMilestone(const QString& title, const QString& dueDate)
{
    return saveMilestone({{QStringLiteral("title"), title}, {QStringLiteral("dueDate"), dueDate}});
}

bool PlannerController::saveMilestone(const QVariantMap& values)
{
    if (!hasWorkspace()) { setFailure(tr("Select a workspace before editing milestones.")); return false; }
    const QString id = values.value(QStringLiteral("id")).toString();
    const QString title = values.value(QStringLiteral("title")).toString().trimmed();
    const QString startText = values.value(QStringLiteral("startDate")).toString().trimmed();
    const QString dueText = values.value(QStringLiteral("dueDate")).toString().trimmed();
    const QDate startDate = parseDate(startText);
    const QDate dueDate = parseDate(dueText);
    if (title.isEmpty() || (!startText.isEmpty() && !startDate.isValid()) ||
        (!dueText.isEmpty() && !dueDate.isValid())) {
        setFailure(tr("Enter a milestone title and valid YYYY-MM-DD dates."));
        return false;
    }
    domain::planner::PlannerMilestone milestone;
    milestone.id = id;
    milestone.workspaceId = m_workspaceId;
    milestone.title = title;
    milestone.description = values.value(QStringLiteral("description")).toString();
    milestone.startDate = startDate;
    milestone.dueDate = dueDate;
    milestone.completed = values.value(QStringLiteral("completed")).toBool();
    if (id.isEmpty()) {
        auto result = m_service.createMilestone(m_workspaceId, milestone);
        if (!result) { setFailure(errorText(result)); return false; }
    } else {
        const auto existing = std::find_if(m_milestones.cbegin(), m_milestones.cend(),
            [&id](const QVariant& value) { return value.toMap().value(QStringLiteral("id")).toString() == id; });
        if (existing == m_milestones.cend()) {
            setFailure(tr("This milestone is no longer available in the active workspace."));
            return false;
        }
        const qint64 revision = existing->toMap().value(QStringLiteral("revision")).toLongLong();
        auto result = m_service.updateMilestone(milestone, revision);
        if (!result) { setFailure(errorText(result)); loadMilestones(); return false; }
    }
    loadMilestones();
    m_error.clear();
    emit changed();
    return true;
}

bool PlannerController::removeMilestone(const QString& milestoneId)
{
    if (!hasWorkspace()) return false;
    const auto existing = std::find_if(m_milestones.cbegin(), m_milestones.cend(),
        [&milestoneId](const QVariant& value) { return value.toMap().value(QStringLiteral("id")).toString() == milestoneId; });
    if (existing == m_milestones.cend()) return false;
    const qint64 revision = existing->toMap().value(QStringLiteral("revision")).toLongLong();
    auto result = m_service.removeMilestone(m_workspaceId, milestoneId, revision);
    if (!result) { setFailure(errorText(result)); return false; }
    if (m_milestoneFilter == milestoneId) m_milestoneFilter.clear();
    reloadAfterMutation();
    m_error.clear();
    return true;
}

QVariantList PlannerController::timeEntries(const QString& taskId)
{
    QVariantList entries;
    if (!hasWorkspace() || !taskById(taskId)) return entries;
    const auto result = m_service.listTimeEntries(m_workspaceId, taskId);
    if (!result) { setFailure(errorText(result)); return entries; }
    for (const auto& entry : result.value())
        entries.append(QVariantMap{{QStringLiteral("id"), entry.id},
            {QStringLiteral("durationMinutes"), entry.durationMinutes},
            {QStringLiteral("note"), entry.note},
            {QStringLiteral("recordedAt"), entry.recordedAtUtc.toLocalTime().toString(Qt::ISODate)}});
    return entries;
}

bool PlannerController::addTimeEntry(const QString& taskId, const QString& minutes, const QString& note)
{
    const auto* task = taskById(taskId);
    if (!hasWorkspace() || !task) return false;
    bool ok = false;
    const qint64 duration = minutes.trimmed().toLongLong(&ok);
    if (!ok || duration <= 0) { setFailure(tr("Enter a time entry duration greater than zero minutes.")); return false; }
    auto result = m_service.addTimeEntry(m_workspaceId, taskId, duration, note.trimmed(), task->revision);
    if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
    m_error.clear();
    reloadAfterMutation();
    return true;
}

bool PlannerController::removeTimeEntry(const QString& taskId, const QString& entryId)
{
    const auto* task = taskById(taskId);
    if (!hasWorkspace() || !task) return false;
    auto result = m_service.removeTimeEntry(m_workspaceId, taskId, entryId, task->revision);
    if (!result) { setFailure(errorText(result)); reloadAfterMutation(); return false; }
    m_error.clear();
    reloadAfterMutation();
    return true;
}

} // namespace devhub::presentation::overview::planner
