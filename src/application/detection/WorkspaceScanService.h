#pragma once

#include "IDetectionResultRepository.h"
#include "WorkspaceScanner.h"
#include "../tasks/TaskScheduler.h"
#include "../workspaces/WorkspaceContext.h"
#include "../settings/SettingsService.h"
#include "../../shared/time/ApplicationClock.h"

#include <QObject>
#include <QHash>

namespace devhub::application::detection {

class WorkspaceScanService final : public QObject
{
    Q_OBJECT
public:
    WorkspaceScanService(tasks::TaskScheduler& scheduler, const WorkspaceScanner& scanner,
        const WorkspaceDetectorRegistry& registry, IDetectionResultRepository& repository,
        workspaces::WorkspaceContext& workspaceContext, shared::time::IApplicationClock& clock,
        ScanBudget budget = {}, settings::SettingsService* settings = nullptr, QObject* parent = nullptr);
    ~WorkspaceScanService() override;
    void schedule(const domain::workspace::Workspace& workspace);
    void cancelAll();

signals:
    void scanFinished(QString workspaceId, bool success, QString message);

private:
    void cancel(const QString& workspaceId);
    tasks::TaskScheduler& m_scheduler;
    const WorkspaceScanner& m_scanner;
    const WorkspaceDetectorRegistry& m_registry;
    IDetectionResultRepository& m_repository;
    workspaces::WorkspaceContext& m_workspaceContext;
    shared::time::IApplicationClock& m_clock;
    ScanBudget m_budget;
    settings::SettingsService* m_settings = nullptr;
    QHash<QString, tasks::TaskHandle> m_handles;
    QHash<QString, quint64> m_generations;
};

} // namespace devhub::application::detection
