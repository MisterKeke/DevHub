#pragma once

#include <QString>
#include <utility>

namespace devhub::domain::workspace {

class WorkspacePath final
{
public:
    WorkspacePath(QString enteredPath, QString canonicalPath)
        : m_enteredPath(std::move(enteredPath)), m_canonicalPath(std::move(canonicalPath)) {}
    [[nodiscard]] const QString& enteredPath() const noexcept { return m_enteredPath; }
    [[nodiscard]] const QString& canonicalPath() const noexcept { return m_canonicalPath; }

private:
    QString m_enteredPath;
    QString m_canonicalPath;
};

} // namespace devhub::domain::workspace
