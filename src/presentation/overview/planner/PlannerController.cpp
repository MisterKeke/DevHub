#include "PlannerController.h"

#include "../../../application/settings/SettingsService.h"
#include "../../../application/workspaces/WorkspaceContext.h"
#include "../../../domain/planner/PlannerRules.h"

#include <QDate>
#include <QQmlEngine>
#include <QSet>
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
    application::workspaces::WorkspaceContext& workspaceContext, QObject* parent)
    : QObject(parent), m_service(service), m_settings(settings), m_workspaceContext(workspaceContext),
      m_tasks(), m_list(), m_calendar(), m_undated()
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
    if (m_workspaceContext.activeWorkspace()) {
        loadWorkspace(m_workspaceContext.activeWorkspace()->id().toString(),
                      m_workspaceContext.activeWorkspace()->displayName());
    }
}

std::optional<PlannerStatus> PlannerController::parseStatus(const QString& value)
{ return domain::planner::plannerStatusFromName(value); }

void PlannerController::workspaceChanged(const QString& id, const QString&)
{
    const auto active = m_workspaceContext.activeWorkspace();
    loadWorkspace(id, id.isEmpty() || !active ? QString{} : active->displayName());
}

void PlannerController::loadWorkspace(const QString& id, const QString& name)
{
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
    if (m_workspaceId.isEmpty() || !m_sessionAllowed) { m_tasks.clear(); emit changed(); return; }
    const QString workspace = m_workspaceId;
    const auto generation = m_loadGeneration;
    m_loading = true;
    emit changed();
    QVector<PlannerTask> all;
    PlannerTaskFilter filter;
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
    for (const auto& milestone : result.value()) m_milestones.append(QVariantMap{
        {QStringLiteral("id"), milestone.id}, {QStringLiteral("title"), milestone.title},
        {QStringLiteral("dueDate"), milestone.dueDate.isValid() ? milestone.dueDate.toString(Qt::ISODate) : QString{}},
        {QStringLiteral("completed"), milestone.completed}, {QStringLiteral("revision"), milestone.revision}});
}

void PlannerController::setSessionAllowed(bool allowed)
{
    if (m_sessionAllowed == allowed) return;
    m_sessionAllowed = allowed;
    ++m_loadGeneration;
    if (!allowed) {
        m_tasks.clear(); m_workspaceId.clear(); m_workspaceName.clear(); m_milestones.clear();
        m_loading = false; m_error.clear(); emit changed(); return;
    }
    const auto active = m_workspaceContext.activeWorkspace();
    loadWorkspace(active ? active->id().toString() : QString{}, active ? active->displayName() : QString{});
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
void PlannerController::setSearchText(const QString& value) { m_searchText = value; applyProxyFilters(); }
void PlannerController::setStatusFilter(const QString& value) { m_statusFilter = value; applyProxyFilters(); }
void PlannerController::setPriorityFilter(const QString& value) { m_priorityFilter = value; applyProxyFilters(); }
void PlannerController::setCompletionFilter(int value) { m_completionFilter = value; applyProxyFilters(); }
void PlannerController::setTagFilter(const QString& value) { m_tagFilter = value; applyProxyFilters(); }
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
    if (row < 0) return {};
    const auto index = m_tasks.index(row, 0);
    QVariantMap values;
    const auto roleNames = m_tasks.roleNames();
    for (auto it = roleNames.cbegin(); it != roleNames.cend(); ++it)
        values.insert(QString::fromUtf8(it.value()), index.data(it.key()));
    return values;
}

QVariantList PlannerController::dependencyChoices(const QString& exclude) const
{
    QVariantList choices;
    for (const auto& task : m_tasks.tasks()) if (task.id != exclude && !task.completed)
        choices.append(QVariantMap{{QStringLiteral("taskId"), task.id}, {QStringLiteral("title"), task.title}});
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

const PlannerTask* PlannerController::taskById(const QString& id) const { return m_tasks.taskById(id); }
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
    bool trackedOk = false; const qint64 tracked = values.value(QStringLiteral("actualTrackedMinutes"), 0).toLongLong(&trackedOk);
    if (!trackedOk || tracked < 0) { setFailure(tr("Tracked time must be a non-negative number of minutes.")); return false; }
    draft.actualTrackedMinutes = tracked;
    draft.milestoneId = values.value(QStringLiteral("milestoneId")).toString();
    draft.gitBranch = values.value(QStringLiteral("gitBranch")).toString();
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
    if (!hasWorkspace() || title.trimmed().isEmpty()) return false;
    if (!dueDate.trimmed().isEmpty() && !QDate::fromString(dueDate.trimmed(), Qt::ISODate).isValid()) {
        setFailure(tr("Enter the milestone due date as a valid YYYY-MM-DD calendar date.")); return false;
    }
    domain::planner::PlannerMilestone milestone; milestone.title = title.trimmed(); milestone.dueDate = parseDate(dueDate);
    auto result = m_service.createMilestone(m_workspaceId, milestone);
    if (!result) { setFailure(errorText(result)); return false; }
    loadMilestones(); m_error.clear(); emit changed(); return true;
}

} // namespace devhub::presentation::overview::planner
