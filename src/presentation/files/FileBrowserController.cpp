#include "FileBrowserController.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace devhub::presentation::files {

void FileBrowserController::setWorkspaceRoot(QString root)
{
    const QFileInfo info(root);
    m_root = info.canonicalFilePath();
    if (m_root.isEmpty()) m_root = QDir::cleanPath(info.absoluteFilePath());
}

QString FileBrowserController::relativePath(const QString& path) const
{
    return QDir(m_root).relativeFilePath(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}

bool FileBrowserController::isWorkspaceRoot(const QString& path) const
{
    QString candidate = QFileInfo(path).canonicalFilePath();
    if (candidate.isEmpty()) candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return candidate.compare(m_root, Qt::CaseInsensitive) == 0;
}

bool FileBrowserController::isInsideWorkspace(const QString& path, bool followLinks) const
{
    const QFileInfo info(path);
    QString candidate = followLinks ? info.canonicalFilePath() : QDir::cleanPath(info.absoluteFilePath());
    if (candidate.isEmpty()) candidate = QDir::cleanPath(info.absoluteFilePath());
    const QString root = QDir::cleanPath(m_root);
    const QString prefix = root.endsWith(QDir::separator()) ? root : root + QDir::separator();
    return candidate.compare(root, Qt::CaseInsensitive) == 0 || candidate.startsWith(prefix, Qt::CaseInsensitive);
}

bool FileBrowserController::validateWindowsName(const QString& name, QString* error) const
{
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"), QRegularExpression::CaseInsensitiveOption);
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) {
        if (error) *error = QStringLiteral("Enter a file or folder name.");
        return false;
    }
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')) || name.contains(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")))) {
        if (error) *error = QStringLiteral("Windows names cannot contain < > : \" / \\ | ? * or end in a dot or space.");
        return false;
    }
    if (reserved.match(name).hasMatch()) {
        if (error) *error = QStringLiteral("That name is reserved by Windows.");
        return false;
    }
    return true;
}

bool FileBrowserController::validateMutationTarget(const QString& path, QString* error) const
{
    if (isWorkspaceRoot(path)) { if (error) *error = QStringLiteral("The workspace root cannot be changed or removed here."); return false; }
    const QFileInfo info(path);
    const QString parent = info.absolutePath();
    if (!isInsideWorkspace(parent, true)) { if (error) *error = QStringLiteral("This operation would leave the workspace."); return false; }
    if ((info.isSymLink() || info.isJunction()) && !isInsideWorkspace(path, true)) { if (error) *error = QStringLiteral("This link points outside the workspace and cannot be changed here."); return false; }
    return true;
}

} // namespace devhub::presentation::files
