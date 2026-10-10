#include "FileBrowserController.h"

namespace devhub::presentation::files {

void FileBrowserController::setWorkspaceRoot(QString root)
{
    m_policy = application::files::WorkspaceFilePolicy(std::move(root));
}
QString FileBrowserController::relativePath(const QString& path) const { return m_policy.relativePath(path); }
bool FileBrowserController::isWorkspaceRoot(const QString& path) const { return m_policy.isWorkspaceRoot(path); }
bool FileBrowserController::isInsideWorkspace(const QString& path, bool followLinks) const { return m_policy.isInsideWorkspace(path, followLinks); }
bool FileBrowserController::validateWindowsName(const QString& name, QString* error) const { return m_policy.validateWindowsName(name, error); }
bool FileBrowserController::validateMutationTarget(const QString& path, QString* error) const { return m_policy.validateMutationTarget(path, error); }

} // namespace devhub::presentation::files
