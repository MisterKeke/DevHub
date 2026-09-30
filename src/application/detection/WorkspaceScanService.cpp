#include "WorkspaceScanService.h"

#include <QPointer>
#include <QUuid>

#include <utility>
#include <variant>

namespace devhub::application::detection {

WorkspaceScanService::WorkspaceScanService(tasks::TaskScheduler& scheduler, const WorkspaceScanner& scanner,
    const WorkspaceDetectorRegistry& registry, IDetectionResultRepository& repository,
    workspaces::WorkspaceContext& workspaceContext, shared::time::IApplicationClock& clock,
    ScanBudget budget, settings::SettingsService* settings, QObject* parent)
    : QObject(parent), m_scheduler(scheduler), m_scanner(scanner), m_registry(registry),
      m_repository(repository), m_workspaceContext(workspaceContext), m_clock(clock), m_budget(std::move(budget)),
      m_settings(settings)
{
    m_scheduler.setModuleConcurrencyLimit(QStringLiteral("workspace-detection"), 1);
    connect(&m_workspaceContext, &workspaces::WorkspaceContext::obsoleteWorkspaceJobsCancelled,
            this, &WorkspaceScanService::cancelAll);
}

WorkspaceScanService::~WorkspaceScanService() { cancelAll(); }

void WorkspaceScanService::cancel(const QString& workspaceId)
{
    const auto it = m_handles.find(workspaceId);
    if (it == m_handles.end()) return;
    it->cancel();
    m_handles.erase(it);
}

void WorkspaceScanService::cancelAll()
{
    for (const auto& handle : std::as_const(m_handles)) handle.cancel();
    m_handles.clear();
}

void WorkspaceScanService::schedule(const domain::workspace::Workspace& workspace)
{
    const QString id = workspace.id().toString();
    cancel(id);
    const QString detectorVersion = m_registry.outputVersion();
    ScanBudget workspaceBudget = m_budget;
    if (m_settings) workspaceBudget.wallTime = std::chrono::milliseconds(
        qBound(100, m_settings->value(QStringLiteral("workspaces/scanLimitMs"), id).toInt(), 60000));
    auto invalidated = m_repository.invalidateIfVersionChanged(workspace.id(), detectorVersion);
    if (!invalidated) {
        emit scanFinished(id, false, invalidated.error().userMessage);
        return;
    }

    const quint64 generation = m_generations.value(id, 0) + 1;
    m_generations.insert(id, generation);
    const auto workspaceToken = m_workspaceContext.currentJobsToken();
    tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("workspace-detection");
    request.workspaceId = QUuid::fromString(id);
    request.generation = generation;
    const QString root = workspace.path().enteredPath();
    QPointer<WorkspaceScanService> self(this);
    auto handle = m_scheduler.submit<ScanReport>(request,
        [scanner = m_scanner, root, budget = workspaceBudget, workspaceToken](const tasks::TaskContext& task) {
            return scanner.scan(root, budget,
                [task, workspaceToken] {
                    return task.cancellationToken.isCancellationRequested() || workspaceToken.isCancellationRequested();
                }, task.reportProgress);
        },
        [self, id, workspaceId = workspace.id(), detectorVersion, workspaceToken](shared::common::TaskOutcome<ScanReport> outcome) mutable {
            if (!self) return;
            self->m_handles.remove(id);
            if (!self->m_workspaceContext.activeWorkspace()
                || self->m_workspaceContext.activeWorkspace()->id() != workspaceId
                || workspaceToken.isCancellationRequested()) return;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                emit self->scanFinished(id, false, QStringLiteral("Workspace scan cancelled."));
                return;
            }
            auto result = std::get<shared::common::Result<ScanReport>>(std::move(outcome));
            if (!result) {
                emit self->scanFinished(id, false, result.error().userMessage);
                return;
            }
            ScanReport report = std::move(result).value();
            auto persisted = self->m_repository.replaceCompletedScan(workspaceId, detectorVersion,
                report.results, report.inspectedFiles, report.limitReached, self->m_clock.utcNow());
            if (!persisted) {
                emit self->scanFinished(id, false, persisted.error().userMessage);
                return;
            }
            const QString message = report.limitReached.isEmpty()
                ? QStringLiteral("Workspace scan complete (%1 files inspected).").arg(report.inspectedFiles)
                : QStringLiteral("Workspace scan stopped at the %1 budget (%2 files inspected).")
                      .arg(report.limitReached).arg(report.inspectedFiles);
            emit self->scanFinished(id, true, message);
        });
    m_handles.insert(id, std::move(handle));
}

} // namespace devhub::application::detection
