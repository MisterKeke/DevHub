#include "PlannerImportController.h"

#include "../../../application/assistant/AssistantToolRuntime.h"
#include "../../../application/authentication/SessionController.h"
#include "../../../application/authentication/SessionState.h"
#include "../../../application/planner/PlannerService.h"
#include "../../../application/settings/SettingsService.h"
#include "../../../application/workspaces/WorkspaceContext.h"
#include "../../../application/workspaces/WorkspaceManager.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSqlError>
#include <QWaitCondition>
#include <QUuid>
#include <QWidget>

#include <algorithm>
#include <memory>
#include <utility>

namespace devhub::presentation::overview::planner {
namespace {
using namespace devhub;
using Reader = infrastructure::planner::SomethingQtImportReader;
using ReaderProfile = infrastructure::planner::SomethingQtImportProfile;
using ReaderPage = infrastructure::planner::SomethingQtImportPage;
using ReaderFilter = infrastructure::planner::SomethingQtImportFilter;
using ReaderSelection = infrastructure::planner::SomethingQtImportSelection;
using ReaderStream = infrastructure::planner::SomethingQtImportStream;
using PlannerTask = domain::planner::PlannerTask;
using TaskContext = application::tasks::TaskContext;
using CancellationToken = application::tasks::CancellationToken;
template<typename T> using TaskOutcome = shared::common::TaskOutcome<T>;

template<typename T>
QString outcomeError(const shared::common::Result<T>& result)
{
    return result.error().userMessage.isEmpty()
        ? QStringLiteral("DevHub could not complete this import operation.") : result.error().userMessage;
}
}

PlannerImportController::PlannerImportController(application::planner::PlannerService& planner,
    application::workspaces::WorkspaceManager& workspaces,
    application::workspaces::WorkspaceContext& workspaceContext,
    application::tasks::TaskScheduler& scheduler,
    application::assistant::AssistantToolRuntime& assistantRuntime,
    application::settings::SettingsService& settings,
    application::authentication::SessionController& session,
    QObject* parent)
    : QObject(parent), m_planner(planner), m_workspaces(workspaces),
      m_workspaceContext(workspaceContext), m_scheduler(scheduler),
      m_assistantRuntime(assistantRuntime), m_settings(settings), m_session(session),
      m_profilePath(assistantRuntime.somethingTasksProfilePath()),
      m_sessionAllowed(session.state() == application::authentication::SessionState::SignedIn)
{
    m_filterTimer.setSingleShot(true);
    m_filterTimer.setInterval(180);
    connect(&m_filterTimer, &QTimer::timeout, this, &PlannerImportController::loadPage);
    connect(&m_workspaceContext, &application::workspaces::WorkspaceContext::workspaceChanged,
        this, [this] {
            if (m_importing) cancelImport();
            if (m_sessionAllowed && m_destinationWorkspaceId.isEmpty()) reloadWorkspaces();
        });
    connect(&m_session, &application::authentication::SessionController::stateChanged, this,
        [this](application::authentication::SessionState state) {
            setSessionAllowed(state == application::authentication::SessionState::SignedIn);
        });
    reloadWorkspaces();
}

PlannerImportController::~PlannerImportController()
{
    if (m_verifyHandle) m_verifyHandle->cancel();
    if (m_pageHandle) m_pageHandle->cancel();
    if (m_previewHandle) m_previewHandle->cancel();
    if (m_prepareHandle) m_prepareHandle->cancel();
    if (m_importHandle) m_importHandle->cancel();
}

void PlannerImportController::openDialog()
{
    m_error.clear();
    m_resultMessage.clear();
    reloadWorkspaces();
    if (!m_profilePath.isEmpty() && !m_verified) verifyConnection();
    emit changed();
}

void PlannerImportController::browseProfile()
{
    const QString start = m_profilePath.isEmpty() ? QString{} : m_profilePath;
    QWidget* parentWidget = qobject_cast<QWidget*>(parent());
    const QString selected = QFileDialog::getOpenFileName(parentWidget,
        tr("Choose SomethingQt profile database"), start,
        tr("SQLite databases (*.sqlite *.sqlite3 *.db);;All files (*)"));
    if (!selected.isEmpty()) setProfilePath(selected);
}

void PlannerImportController::setProfilePath(const QString& input)
{
    const QString path = input.trimmed().isEmpty() ? QString{} : QFileInfo(input.trimmed()).absoluteFilePath();
    if (m_profilePath == path) return;
    if (m_verifyHandle) m_verifyHandle->cancel();
    if (m_pageHandle) m_pageHandle->cancel();
    if (m_previewHandle) m_previewHandle->cancel();
    if (m_prepareHandle) m_prepareHandle->cancel();
    if (m_importHandle) cancelImport();
    ++m_verifyGeneration; ++m_pageGeneration; ++m_previewGeneration; ++m_prepareGeneration;
    m_verifyHandle.reset(); m_pageHandle.reset(); m_previewHandle.reset(); m_prepareHandle.reset();
    m_loading = false; m_preparing = false; m_confirmationReady = false; m_expectedCount = 0;
    m_profilePath = path;
    m_verified = false;
    m_profileIdentity.clear();
    m_connectionStatus = path.isEmpty() ? tr("No source selected") : tr("Not verified");
    m_tasks.clear(); m_sourceTags.clear(); m_preview.clear(); m_selectedIds.clear(); m_importTags.clear();
    m_totalCount = 0; m_page = 0;
    m_error.clear();
    const auto saved = m_settings.setValue(QStringLiteral("assistant/somethingQtProfilePath"), path);
    if (!saved) { setFailure(saved.error().userMessage); return; }
    m_assistantRuntime.setSomethingTasksProfilePath(path);
    emit changed();
}

void PlannerImportController::verifyConnection()
{
    if (!m_sessionAllowed) { setFailure(tr("Unlock DevHub before browsing or importing tasks.")); return; }
    if (m_profilePath.isEmpty()) { setFailure(tr("Choose a SomethingQt database file first.")); return; }
    if (m_verifyHandle) m_verifyHandle->cancel();
    const quint64 generation = ++m_verifyGeneration;
    const QString path = m_profilePath;
    application::tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("planner-somethingqt-verify");
    QPointer<PlannerImportController> self(this);
    m_loading = true; m_verified = false; m_connectionStatus = tr("Checking read-only access…");
    m_error.clear(); m_tasks.clear(); m_preview.clear(); m_totalCount = 0;
    emit changed();
    m_verifyHandle = m_scheduler.submit<infrastructure::planner::SomethingQtImportRead<ReaderProfile>>(
        request, [path](const TaskContext& context) {
            return shared::common::Result<infrastructure::planner::SomethingQtImportRead<ReaderProfile>>::success(
                Reader::inspect(path, context.cancellationToken));
        }, [self, generation, path](TaskOutcome<infrastructure::planner::SomethingQtImportRead<ReaderProfile>> outcome) {
            if (!self || generation != self->m_verifyGeneration || self->m_profilePath != path) return;
            self->m_verifyHandle.reset(); self->m_loading = false;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                self->m_connectionStatus = self->tr("Verification cancelled"); self->emit changed(); return;
            }
            const auto result = std::get<shared::common::Result<infrastructure::planner::SomethingQtImportRead<ReaderProfile>>>(std::move(outcome));
            if (!result) { self->setFailure(outcomeError(result)); return; }
            const auto& read = result.value();
            if (!read.success) {
                self->m_connectionStatus = read.message;
                self->setFailure(read.message); return;
            }
            self->m_profileIdentity = read.value.identity;
            self->m_sourceTags = read.value.tags;
            self->m_totalCount = read.value.taskCount;
            self->m_verified = true;
            self->m_connectionStatus = self->tr("Connected read-only · schema v%1 · %2 tasks")
                .arg(read.value.schemaVersion).arg(read.value.taskCount);
            self->m_selectedIds.clear(); self->m_importTags.clear(); self->m_page = 0;
            self->schedulePageLoad(false);
            self->emit changed();
        });
}

void PlannerImportController::setSearchText(const QString& text)
{ if (m_searchText == text) return; m_searchText = text; schedulePageLoad(); }
void PlannerImportController::setTagFilter(const QString& tag)
{ if (m_tagFilter == tag) return; m_tagFilter = tag; schedulePageLoad(); }
void PlannerImportController::setPriorityFilter(const QString& priority)
{ if (m_priorityFilter == priority) return; m_priorityFilter = priority; schedulePageLoad(); }
void PlannerImportController::setCompletionFilter(int completion)
{ if (completion < -1 || completion > 1 || m_completionFilter == completion) return; m_completionFilter = completion; schedulePageLoad(); }

void PlannerImportController::schedulePageLoad(bool resetPage)
{
    if (resetPage) m_page = 0;
    ++m_pageGeneration;
    if (!m_verified) { emit changed(); return; }
    if (m_pageHandle) m_pageHandle->cancel();
    m_filterTimer.start();
    emit changed();
}

void PlannerImportController::loadPage()
{
    if (!m_verified || !m_sessionAllowed || m_importing) return;
    if (m_pageHandle) m_pageHandle->cancel();
    const quint64 generation = ++m_pageGeneration;
    const QString path = m_profilePath;
    const QString identity = m_profileIdentity;
    ReaderFilter filter;
    filter.searchText = m_searchText; filter.tag = m_tagFilter; filter.priority = m_priorityFilter;
    filter.completion = m_completionFilter; filter.offset = m_page * pageSize(); filter.limit = pageSize();
    application::tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("planner-somethingqt-page");
    m_loading = true; m_error.clear(); emit changed();
    QPointer<PlannerImportController> self(this);
    m_pageHandle = m_scheduler.submit<infrastructure::planner::SomethingQtImportRead<ReaderPage>>(
        request, [path, identity, filter](const TaskContext& context) {
            return shared::common::Result<infrastructure::planner::SomethingQtImportRead<ReaderPage>>::success(
                Reader::listPage(path, identity, filter, context.cancellationToken));
        }, [self, generation, path](TaskOutcome<infrastructure::planner::SomethingQtImportRead<ReaderPage>> outcome) {
            if (!self || generation != self->m_pageGeneration || self->m_profilePath != path) return;
            self->m_pageHandle.reset(); self->m_loading = false;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) { self->emit changed(); return; }
            const auto result = std::get<shared::common::Result<infrastructure::planner::SomethingQtImportRead<ReaderPage>>>(std::move(outcome));
            if (!result) { self->setFailure(outcomeError(result)); return; }
            const auto& read = result.value();
            if (!read.success) {
                if (read.errorCode == QStringLiteral("profile_changed")) self->invalidateSource(read.message);
                else self->setFailure(read.message);
                return;
            }
            self->m_totalCount = read.value.totalCount;
            auto importCounts = self->m_planner.importedSourceTaskCounts(self->m_destinationWorkspaceId, self->m_profileIdentity);
            self->m_importCounts = importCounts ? importCounts.value() : QHash<QString, int>{};
            self->m_tasks.clear();
            for (const auto& item : read.value.tasks)
                self->m_tasks.append(summaryMap(item, self->m_selectedIds.contains(item.sourceTaskId), self->m_importCounts.value(item.sourceTaskId)));
            self->emit changed();
        });
}

void PlannerImportController::previousPage()
{ if (m_page > 0) { --m_page; schedulePageLoad(false); } }
void PlannerImportController::nextPage()
{ if ((static_cast<qint64>(m_page) + 1) * pageSize() < m_totalCount) { ++m_page; schedulePageLoad(false); } }

void PlannerImportController::selectTask(const QString& id, bool selected)
{
    if (selected) m_selectedIds.insert(id); else m_selectedIds.remove(id);
    for (QVariant& row : m_tasks) {
        QVariantMap values = row.toMap();
        if (values.value(QStringLiteral("sourceTaskId")).toString() == id) {
            values.insert(QStringLiteral("selected"), selected); row = values; break;
        }
    }
    emit changed();
}

void PlannerImportController::selectAllVisible()
{
    for (QVariant& row : m_tasks) {
        QVariantMap values = row.toMap();
        const QString id = values.value(QStringLiteral("sourceTaskId")).toString();
        m_selectedIds.insert(id); values.insert(QStringLiteral("selected"), true); row = values;
    }
    emit changed();
}

void PlannerImportController::clearSelection()
{
    m_selectedIds.clear();
    for (QVariant& row : m_tasks) { QVariantMap values = row.toMap(); values.insert(QStringLiteral("selected"), false); row = values; }
    emit changed();
}

void PlannerImportController::previewTask(const QString& sourceTaskId)
{
    if (!m_verified || !m_sessionAllowed) return;
    if (m_previewHandle) m_previewHandle->cancel();
    const quint64 generation = ++m_previewGeneration;
    const QString path = m_profilePath, identity = m_profileIdentity;
    m_preview.clear(); m_error.clear(); m_loading = true; emit changed();
    QPointer<PlannerImportController> self(this);
    application::tasks::TaskRequest request; request.moduleKey = QStringLiteral("planner-somethingqt-preview");
    m_previewHandle = m_scheduler.submit<infrastructure::planner::SomethingQtImportRead<PlannerTask>>(
        request, [path, identity, sourceTaskId](const TaskContext& context) {
            return shared::common::Result<infrastructure::planner::SomethingQtImportRead<PlannerTask>>::success(
                Reader::readTask(path, identity, sourceTaskId, context.cancellationToken));
        }, [self, generation, path](TaskOutcome<infrastructure::planner::SomethingQtImportRead<PlannerTask>> outcome) {
            if (!self || generation != self->m_previewGeneration || self->m_profilePath != path) return;
            self->m_previewHandle.reset(); self->m_loading = false;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) { self->emit changed(); return; }
            const auto result = std::get<shared::common::Result<infrastructure::planner::SomethingQtImportRead<PlannerTask>>>(std::move(outcome));
            if (!result) { self->setFailure(outcomeError(result)); return; }
            const auto& read = result.value();
            if (!read.success) {
                if (read.errorCode == QStringLiteral("profile_changed")) self->invalidateSource(read.message);
                else self->setFailure(read.message);
                return;
            }
            self->m_preview = previewMap(read.value); self->emit changed();
        });
}

void PlannerImportController::setDestinationWorkspaceId(const QString& id)
{
    if (m_destinationWorkspaceId == id) return;
    if (m_importing) cancelImport();
    m_destinationWorkspaceId = id; m_destinationWorkspaceName.clear();
    for (const QVariant& row : std::as_const(m_workspaceOptions)) {
        const auto option = row.toMap();
        if (option.value(QStringLiteral("id")).toString() == id) {
            m_destinationWorkspaceName = option.value(QStringLiteral("name")).toString(); break;
        }
    }
    m_importCounts.clear();
    if (m_verified && !id.isEmpty()) {
        auto counts = m_planner.importedSourceTaskCounts(id, m_profileIdentity);
        if (counts) m_importCounts = counts.value();
        for (QVariant& row : m_tasks) {
            QVariantMap values = row.toMap();
            values.insert(QStringLiteral("importCount"), m_importCounts.value(values.value(QStringLiteral("sourceTaskId")).toString()));
            row = values;
        }
    }
    emit changed();
}

void PlannerImportController::toggleImportTag(const QString& tag, bool selected)
{ if (selected) m_importTags.insert(tag); else m_importTags.remove(tag); emit changed(); }
bool PlannerImportController::isImportTagSelected(const QString& tag) const { return m_importTags.contains(tag); }

ReaderSelection PlannerImportController::selectionForMode(const QString& mode) const
{
    ReaderSelection selection;
    if (mode == QStringLiteral("selected")) selection.sourceTaskIds = m_selectedIds.values();
    else if (mode == QStringLiteral("tags")) selection.tags = m_importTags.values();
    else if (mode == QStringLiteral("all")) selection.all = true;
    return selection;
}

void PlannerImportController::prepareImport(const QString& mode)
{
    if (!m_verified || !m_sessionAllowed || m_importing) { setFailure(tr("Verify the SomethingQt profile and unlock DevHub before importing.")); return; }
    if (!destinationStillValid(m_destinationWorkspaceId)) { setFailure(tr("Choose an available destination Workspace.")); return; }
    if (mode != QStringLiteral("selected") && mode != QStringLiteral("tags") && mode != QStringLiteral("all")) { setFailure(tr("Choose an import mode.")); return; }
    const ReaderSelection selection = selectionForMode(mode);
    if (mode == QStringLiteral("selected") && selection.sourceTaskIds.isEmpty()) { setFailure(tr("Select one or more visible tasks first.")); return; }
    if (mode == QStringLiteral("tags") && selection.tags.isEmpty()) { setFailure(tr("Select at least one tag for the tag import.")); return; }
    if (m_prepareHandle) m_prepareHandle->cancel();
    const quint64 generation = ++m_prepareGeneration;
    const QString path = m_profilePath, identity = m_profileIdentity;
    m_preparing = true; m_loading = true; m_confirmationReady = false; m_importMode = mode; m_error.clear(); emit changed();
    QPointer<PlannerImportController> self(this);
    application::tasks::TaskRequest request; request.moduleKey = QStringLiteral("planner-somethingqt-count");
    m_prepareHandle = m_scheduler.submit<infrastructure::planner::SomethingQtImportRead<qint64>>(
        request, [path, identity, selection](const TaskContext& context) {
            return shared::common::Result<infrastructure::planner::SomethingQtImportRead<qint64>>::success(
                Reader::countSelection(path, identity, selection, context.cancellationToken));
        }, [self, generation, path](TaskOutcome<infrastructure::planner::SomethingQtImportRead<qint64>> outcome) {
            if (!self || generation != self->m_prepareGeneration || self->m_profilePath != path) return;
            self->m_prepareHandle.reset(); self->m_preparing = false; self->m_loading = false;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) { self->emit changed(); return; }
            const auto result = std::get<shared::common::Result<infrastructure::planner::SomethingQtImportRead<qint64>>>(std::move(outcome));
            if (!result) { self->setFailure(outcomeError(result)); return; }
            const auto& read = result.value();
            if (!read.success) {
                if (read.errorCode == QStringLiteral("profile_changed")) self->invalidateSource(read.message);
                else self->setFailure(read.message);
                return;
            }
            self->m_expectedCount = read.value; self->m_confirmationReady = true;
            self->emit changed(); emit self->confirmationReady(self->m_expectedCount);
        });
}

void PlannerImportController::startImport(bool allowAdditionalCopies)
{
    if (!m_confirmationReady || m_importing || !m_sessionAllowed) return;
    if (!destinationStillValid(m_destinationWorkspaceId)) { setFailure(tr("The destination Workspace is no longer available. Choose another Workspace.")); return; }
    const QString destination = m_destinationWorkspaceId;
    const QString path = m_profilePath, identity = m_profileIdentity;
    const ReaderSelection selection = selectionForMode(m_importMode);
    m_confirmationReady = false; m_importing = true; m_progress = 0;
    m_progressMessage = tr("Starting import…"); m_error.clear(); m_resultMessage.clear();
    m_importedCount = 0; m_skippedCount = 0;
    auto existing = m_planner.importedSourceTaskCounts(destination, identity);
    m_importCounts = existing ? existing.value() : QHash<QString, int>{};
    const quint64 generation = ++m_importGeneration;
    application::tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("planner-somethingqt-import");
    QPointer<PlannerImportController> self(this);
    m_importHandle = m_scheduler.submit<ReaderStream>(request,
        [self, path, identity, selection, destination, allowAdditionalCopies](const TaskContext& context) {
            return shared::common::Result<ReaderStream>::success(Reader::forEachTask(
                path, identity, selection, context.cancellationToken,
                [self, destination, allowAdditionalCopies, cancellation = context.cancellationToken](PlannerTask task) mutable {
                    if (!self || cancellation.isCancellationRequested()) return false;
                    struct Ack final { QMutex mutex; QWaitCondition ready; bool done = false; bool keepGoing = false; };
                    auto ack = std::make_shared<Ack>();
                    const bool invoked = QMetaObject::invokeMethod(self.data(), [self, ack, task = std::move(task), destination, allowAdditionalCopies, cancellation]() mutable {
                        const bool keepGoing = self && self->commitOne(std::move(task), destination, allowAdditionalCopies, cancellation);
                        QMutexLocker lock(&ack->mutex);
                        ack->keepGoing = keepGoing;
                        ack->done = true;
                        ack->ready.wakeAll();
                    }, Qt::QueuedConnection);
                    if (!invoked) return false;
                    QMutexLocker lock(&ack->mutex);
                    while (!ack->done && !cancellation.isCancellationRequested())
                        ack->ready.wait(&ack->mutex, 50);
                    return ack->done && ack->keepGoing;
                }, [self](qint64 current, qint64 total) {
                    if (!self) return;
                    QMetaObject::invokeMethod(self.data(), [self, current, total] {
                        if (!self) return;
                        self->m_progress = total > 0 ? static_cast<int>((current * 100) / total) : 100;
                        self->m_progressMessage = self->tr("Processed %1 of %2 source tasks").arg(current).arg(total);
                        self->emit changed();
                    }, Qt::QueuedConnection);
                }));
        }, [self, generation](TaskOutcome<ReaderStream> outcome) {
            if (!self || generation != self->m_importGeneration) return;
            self->m_importHandle.reset(); self->m_importing = false;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                self->m_resultMessage = self->tr("Import cancelled after %1 imported; %2 duplicate tasks skipped. Saved tasks remain complete.")
                    .arg(self->m_importedCount).arg(self->m_skippedCount);
                self->m_progressMessage = self->tr("Cancelled at a task boundary");
                self->m_progress = 0; self->emit changed();
                if (self->m_importedCount > 0) emit self->tasksImported();
                return;
            }
            const auto result = std::get<shared::common::Result<ReaderStream>>(std::move(outcome));
            if (!result) { self->setFailure(outcomeError(result)); return; }
            const ReaderStream& stream = result.value();
            if (stream.success) {
                self->m_progress = 100;
                self->m_resultMessage = self->tr("Imported %1 tasks; skipped %2 duplicates.")
                    .arg(self->m_importedCount).arg(self->m_skippedCount);
                self->m_progressMessage = self->m_resultMessage;
            } else if (stream.cancelled) {
                self->m_resultMessage = self->tr("Import cancelled after %1 imported; %2 duplicate tasks skipped. Saved tasks remain complete.")
                    .arg(self->m_importedCount).arg(self->m_skippedCount);
                self->m_progressMessage = self->tr("Cancelled at a task boundary");
            } else if (stream.errorCode == QStringLiteral("target_failed") && !self->m_error.isEmpty()) {
                self->m_resultMessage = self->tr("Import stopped after %1 imported; %2 duplicates skipped.")
                    .arg(self->m_importedCount).arg(self->m_skippedCount);
            } else {
                self->m_error = stream.message;
                self->m_resultMessage = self->tr("Import stopped after %1 imported; %2 duplicates skipped.")
                    .arg(self->m_importedCount).arg(self->m_skippedCount);
                if (stream.errorCode == QStringLiteral("profile_changed")) {
                    self->m_verified = false;
                    self->m_profileIdentity.clear();
                    self->m_tasks.clear(); self->m_sourceTags.clear(); self->m_preview.clear();
                    self->m_selectedIds.clear(); self->m_importTags.clear(); self->m_totalCount = 0;
                    self->m_connectionStatus = stream.message;
                }
            }
            self->emit changed();
            if (self->m_importedCount > 0) emit self->tasksImported();
            self->schedulePageLoad(false);
        });
    emit changed();
}

bool PlannerImportController::commitOne(PlannerTask task, const QString& workspaceId,
    bool allowAdditionalCopies, const CancellationToken& cancellation)
{
    if (cancellation.isCancellationRequested()) return false;
    if (!destinationStillValid(workspaceId)) {
        m_error = tr("The destination Workspace was archived, removed, or is no longer authorized. Import stopped before saving the next task.");
        return false;
    }
    if (!allowAdditionalCopies && m_importCounts.value(task.sourceTaskId) > 0) {
        ++m_skippedCount;
        return true;
    }
    const QString sourceTaskId = task.sourceTaskId;
    const auto imported = m_planner.importTask(workspaceId, std::move(task), allowAdditionalCopies);
    if (!imported) {
        if (!allowAdditionalCopies && imported.error().userMessage.contains(QStringLiteral("already been imported"), Qt::CaseInsensitive)) {
            ++m_skippedCount; ++m_importCounts[sourceTaskId];
            return true;
        }
        m_error = outcomeError(imported); return false;
    }
    const QString sourceId = imported.value().sourceTaskId;
    ++m_importedCount; ++m_importCounts[sourceId];
    return true;
}

void PlannerImportController::cancelImport()
{
    if (m_importHandle) {
        m_importHandle->cancel();
        m_progressMessage = tr("Cancelling after the current task…");
        emit changed();
    }
}

void PlannerImportController::closeDialog()
{
    if (m_importing) cancelImport();
    if (m_previewHandle) m_previewHandle->cancel();
    if (m_prepareHandle) m_prepareHandle->cancel();
    m_prepareHandle.reset();
    ++m_prepareGeneration;
    m_preparing = false;
    m_confirmationReady = false;
    emit changed();
}

void PlannerImportController::setSessionAllowed(bool allowed)
{
    if (m_sessionAllowed == allowed) return;
    m_sessionAllowed = allowed;
    if (!allowed) {
        if (m_verifyHandle) m_verifyHandle->cancel();
        if (m_pageHandle) m_pageHandle->cancel();
        if (m_previewHandle) m_previewHandle->cancel();
        if (m_prepareHandle) m_prepareHandle->cancel();
        cancelImport();
        m_verifyHandle.reset(); m_pageHandle.reset(); m_previewHandle.reset(); m_prepareHandle.reset();
        if (m_importHandle) m_importHandle.reset();
        ++m_verifyGeneration; ++m_pageGeneration; ++m_previewGeneration; ++m_prepareGeneration; ++m_importGeneration;
        m_verified = false; m_profileIdentity.clear(); m_connectionStatus = tr("Unlock DevHub to continue");
        m_tasks.clear(); m_sourceTags.clear(); m_preview.clear(); m_selectedIds.clear(); m_importTags.clear();
        m_importCounts.clear(); m_totalCount = 0; m_expectedCount = 0; m_confirmationReady = false;
        m_workspaceOptions.clear(); m_destinationWorkspaceId.clear(); m_destinationWorkspaceName.clear();
        m_loading = false; m_preparing = false; m_importing = false; m_progress = 0;
        m_progressMessage.clear(); m_error.clear(); m_resultMessage.clear();
    } else {
        m_connectionStatus = m_profilePath.isEmpty() ? tr("No source selected") : tr("Not verified");
        reloadWorkspaces();
    }
    emit changed();
}

void PlannerImportController::reloadWorkspaces()
{
    m_workspaceOptions.clear();
    if (!m_sessionAllowed) { emit changed(); return; }
    auto result = m_workspaces.list();
    if (!result) { setFailure(outcomeError(result)); return; }
    for (const auto& workspace : result.value()) {
        m_workspaceOptions.append(QVariantMap{{QStringLiteral("id"), workspace.id().toString()},
            {QStringLiteral("name"), workspace.displayName()}});
    }
    const auto active = m_workspaceContext.activeWorkspace();
    if (m_destinationWorkspaceId.isEmpty() && active) setDestinationWorkspaceId(active->id().toString());
    if (!m_destinationWorkspaceId.isEmpty()) {
        bool found = false;
        for (const QVariant& option : std::as_const(m_workspaceOptions)) {
            const QVariantMap values = option.toMap();
            if (values.value(QStringLiteral("id")).toString() == m_destinationWorkspaceId) {
                found = true; m_destinationWorkspaceName = values.value(QStringLiteral("name")).toString(); break;
            }
        }
        if (!found) { m_destinationWorkspaceId.clear(); m_destinationWorkspaceName.clear(); }
    }
    emit changed();
}

bool PlannerImportController::destinationStillValid(const QString& workspaceId) const
{
    if (!m_sessionAllowed || m_session.state() != application::authentication::SessionState::SignedIn || workspaceId.isEmpty()) return false;
    const auto result = m_workspaces.list();
    if (!result) return false;
    return std::any_of(result.value().cbegin(), result.value().cend(), [&workspaceId](const auto& workspace) {
        return workspace.id().toString() == workspaceId && !workspace.isArchived();
    });
}

void PlannerImportController::invalidateSource(QString status)
{
    m_verified = false; m_profileIdentity.clear(); m_tasks.clear(); m_preview.clear(); m_sourceTags.clear();
    m_selectedIds.clear(); m_importTags.clear(); m_totalCount = 0; m_connectionStatus = std::move(status);
    setFailure(m_connectionStatus);
}

void PlannerImportController::setFailure(QString message)
{ m_error = std::move(message); emit changed(); }

QVariantMap PlannerImportController::summaryMap(const infrastructure::planner::SomethingQtImportSummary& item,
    bool selected, int importCount)
{
    return {{QStringLiteral("sourceTaskId"), item.sourceTaskId}, {QStringLiteral("title"), item.title},
        {QStringLiteral("completed"), item.completed}, {QStringLiteral("priority"), item.priority},
        {QStringLiteral("difficulty"), item.difficulty}, {QStringLiteral("dueDate"), item.dueDate},
        {QStringLiteral("createdAt"), item.createdAt}, {QStringLiteral("updatedAt"), item.updatedAt},
        {QStringLiteral("revision"), item.revision}, {QStringLiteral("tags"), item.tags},
        {QStringLiteral("selected"), selected}, {QStringLiteral("importCount"), importCount}};
}

QVariantMap PlannerImportController::previewMap(const PlannerTask& task)
{
    QVariantList subtasks;
    for (const auto& item : task.subtasks)
        subtasks.append(QVariantMap{{QStringLiteral("sourceId"), item.sourceSubtaskId},
            {QStringLiteral("title"), item.title}, {QStringLiteral("completed"), item.completed},
            {QStringLiteral("position"), item.position}});
    return {{QStringLiteral("sourceTaskId"), task.sourceTaskId}, {QStringLiteral("title"), task.title},
        {QStringLiteral("description"), task.description}, {QStringLiteral("completed"), task.completed},
        {QStringLiteral("priority"), task.priority}, {QStringLiteral("difficulty"), task.difficulty},
        {QStringLiteral("dueDate"), task.dueDate.isValid() ? task.dueDate.toString(Qt::ISODate) : QString{}},
        {QStringLiteral("tags"), task.tags}, {QStringLiteral("subtasks"), subtasks},
        {QStringLiteral("createdAt"), task.sourceCreatedAt}, {QStringLiteral("updatedAt"), task.sourceUpdatedAt},
        {QStringLiteral("revision"), task.sourceRevision}};
}

} // namespace devhub::presentation::overview::planner
