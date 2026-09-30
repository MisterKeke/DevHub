#pragma once

#include "../../domain/workspace/Workspace.h"
#include "../tasks/Cancellation.h"

#include <QObject>
#include <optional>

namespace devhub::application::workspaces {

class WorkspaceContext final : public QObject
{
    Q_OBJECT
public:
    explicit WorkspaceContext(QObject* parent = nullptr) : QObject(parent) {}
    [[nodiscard]] const std::optional<domain::workspace::Workspace>& activeWorkspace() const noexcept { return m_active; }
    [[nodiscard]] tasks::CancellationToken currentJobsToken() const { return m_jobs.token(); }
    void activate(domain::workspace::Workspace workspace);
    void clear();

signals:
    void workspaceChanged(QString workspaceId, QString absolutePath);
    void obsoleteWorkspaceJobsCancelled();

private:
    std::optional<domain::workspace::Workspace> m_active;
    tasks::CancellationSource m_jobs;
};

} // namespace devhub::application::workspaces
