#pragma once

#include <QString>
#include <optional>

namespace devhub::application::files {

// Immutable workspace authority copied into bounded file operations.
class WorkspaceFilePolicy final
{
public:
    explicit WorkspaceFilePolicy(QString workspaceRoot = {});
    [[nodiscard]] QString workspaceRoot() const { return m_root; }
    [[nodiscard]] QString relativePath(const QString& path) const;
    [[nodiscard]] bool isWorkspaceRoot(const QString& path) const;
    [[nodiscard]] bool isInsideWorkspace(const QString& path, bool followLinks = true) const;
    [[nodiscard]] bool validateWindowsName(const QString& name, QString* error = nullptr) const;
    [[nodiscard]] bool validateMutationTarget(const QString& path, QString* error = nullptr) const;
    [[nodiscard]] std::optional<QString> resolveReadPath(const QString& relativePath, QString* error = nullptr) const;
    [[nodiscard]] static bool isSensitivePathComponent(const QString& component);
    [[nodiscard]] static bool isPrunedSearchDirectory(const QString& component);
    [[nodiscard]] static bool hasLinkComponent(const QString& absolutePath);
private:
    QString m_root;
};

} // namespace devhub::application::files
