#pragma once

#include <QString>

namespace devhub::presentation::files {

class FileBrowserController final
{
public:
    void setWorkspaceRoot(QString root);
    [[nodiscard]] QString workspaceRoot() const { return m_root; }
    [[nodiscard]] QString relativePath(const QString& path) const;
    [[nodiscard]] bool isWorkspaceRoot(const QString& path) const;
    [[nodiscard]] bool isInsideWorkspace(const QString& path, bool followLinks = true) const;
    [[nodiscard]] bool validateWindowsName(const QString& name, QString* error = nullptr) const;
    [[nodiscard]] bool validateMutationTarget(const QString& path, QString* error = nullptr) const;
private:
    QString m_root;
};

} // namespace devhub::presentation::files
