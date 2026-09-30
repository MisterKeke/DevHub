#include "WorkspaceContext.h"

namespace devhub::application::workspaces {

void WorkspaceContext::activate(domain::workspace::Workspace workspace)
{
    if (m_active && m_active->id() == workspace.id()) return;
    m_jobs.requestCancellation();
    emit obsoleteWorkspaceJobsCancelled();
    m_jobs = tasks::CancellationSource();
    const QString id = workspace.id().toString();
    const QString path = workspace.path().canonicalPath();
    m_active = std::move(workspace);
    emit workspaceChanged(id, path);
}

void WorkspaceContext::clear()
{
    if (!m_active) return;
    m_jobs.requestCancellation();
    emit obsoleteWorkspaceJobsCancelled();
    m_jobs = tasks::CancellationSource();
    m_active.reset();
    emit workspaceChanged({}, {});
}

} // namespace devhub::application::workspaces
