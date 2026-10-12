#include "WorkspaceFilePolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace devhub::application::files {

bool WorkspaceFilePolicy::isSensitivePathComponent(const QString& component)
{
    const QString lower = component.toLower();
    return lower == QStringLiteral(".ssh") || lower == QStringLiteral(".aws") || lower == QStringLiteral(".azure")
        || lower == QStringLiteral(".codex") || lower == QStringLiteral(".gnupg") || lower == QStringLiteral(".config")
        || lower == QStringLiteral("credentials") || lower == QStringLiteral("secrets") || lower == QStringLiteral(".env")
        || lower.startsWith(QStringLiteral(".env.")) || lower == QStringLiteral("id_rsa") || lower == QStringLiteral("id_ed25519")
        || lower == QStringLiteral("id_ecdsa") || lower == QStringLiteral(".npmrc") || lower == QStringLiteral(".pypirc")
        || lower == QStringLiteral(".netrc") || lower == QStringLiteral("dockerconfig.json")
        || lower.endsWith(QStringLiteral(".pem")) || lower.endsWith(QStringLiteral(".p12"))
        || lower.endsWith(QStringLiteral(".pfx")) || lower.endsWith(QStringLiteral(".key"))
        || lower.endsWith(QStringLiteral(".ppk")) || lower.endsWith(QStringLiteral(".keystore"));
}

bool WorkspaceFilePolicy::isPrunedSearchDirectory(const QString& component)
{
    static const QSet<QString> pruned{QStringLiteral(".git"), QStringLiteral(".hg"), QStringLiteral(".svn"),
        QStringLiteral(".ssh"), QStringLiteral(".aws"), QStringLiteral(".azure"), QStringLiteral(".config"),
        QStringLiteral(".codex"), QStringLiteral(".gnupg"), QStringLiteral(".cache"), QStringLiteral("cache"),
        QStringLiteral("credentials"), QStringLiteral("secrets"), QStringLiteral(".kube"), QStringLiteral(".docker"),
        QStringLiteral(".npm"), QStringLiteral(".thunderbird"), QStringLiteral(".mozilla"),
        QStringLiteral("node_modules"), QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral("__pycache__")};
    return pruned.contains(component.toLower());
}

bool WorkspaceFilePolicy::hasLinkComponent(const QString& absolutePath)
{
    QFileInfo current(absolutePath);
    while (current.exists() || current.isSymLink() || current.isJunction()) {
        if (current.isSymLink() || current.isJunction()) return true;
        const QString parent = current.absolutePath();
        if (parent == current.absoluteFilePath()) break;
        current.setFile(parent);
    }
    return false;
}

WorkspaceFilePolicy::WorkspaceFilePolicy(QString root)
{
    if (root.isEmpty()) return;
    const QFileInfo info(root);
    m_root = info.canonicalFilePath();
    if (m_root.isEmpty()) m_root = QDir::cleanPath(info.absoluteFilePath());
}

QString WorkspaceFilePolicy::relativePath(const QString& path) const
{
    return m_root.isEmpty() ? QString{} : QDir(m_root).relativeFilePath(QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath())));
}

bool WorkspaceFilePolicy::isWorkspaceRoot(const QString& path) const
{
    if (m_root.isEmpty()) return false;
    QString candidate = QFileInfo(path).canonicalFilePath();
    if (candidate.isEmpty()) candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    candidate = QDir::fromNativeSeparators(candidate);
    return candidate.compare(QDir::fromNativeSeparators(m_root), Qt::CaseInsensitive) == 0;
}

bool WorkspaceFilePolicy::isInsideWorkspace(const QString& path, bool followLinks) const
{
    if (m_root.isEmpty() || path.isEmpty()) return false;
    const QFileInfo info(path);
    QString candidate = followLinks ? info.canonicalFilePath() : QDir::cleanPath(info.absoluteFilePath());
    if (followLinks && (info.isSymLink() || info.isJunction())) {
        const QString target = info.symLinkTarget();
        if (target.isEmpty()) return false;
        const QFileInfo targetInfo(target);
        candidate = targetInfo.canonicalFilePath();
        if (candidate.isEmpty()) candidate = targetInfo.absoluteFilePath();
    }
    if (candidate.isEmpty()) candidate = QDir::cleanPath(info.absoluteFilePath());
    candidate = QDir::fromNativeSeparators(candidate);
    const QString root = QDir::fromNativeSeparators(QDir::cleanPath(m_root));
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return candidate.compare(root, Qt::CaseInsensitive) == 0 || candidate.startsWith(prefix, Qt::CaseInsensitive);
}

bool WorkspaceFilePolicy::validateWindowsName(const QString& name, QString* error) const
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

bool WorkspaceFilePolicy::validateMutationTarget(const QString& path, QString* error) const
{
    if (isWorkspaceRoot(path)) { if (error) *error = QStringLiteral("The workspace root cannot be changed or removed here."); return false; }
    const QFileInfo info(path);
    const QString parent = info.absolutePath();
    if (!isInsideWorkspace(parent, true)) { if (error) *error = QStringLiteral("This operation would leave the workspace."); return false; }
    if ((info.isSymLink() || info.isJunction()) && !isInsideWorkspace(path, true)) { if (error) *error = QStringLiteral("This link points outside the workspace and cannot be changed here."); return false; }
    return true;
}

std::optional<QString> WorkspaceFilePolicy::resolveReadPath(const QString& relative, QString* error) const
{
    const QString clean = QDir::fromNativeSeparators(relative);
    const QStringList components = clean.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (m_root.isEmpty() || clean.isEmpty() || QDir::isAbsolutePath(clean) || clean.startsWith(QLatin1Char('/'))
        || clean.startsWith(QStringLiteral("//")) || clean.contains(QLatin1Char(':'))
        || clean.contains(QChar(0)) || clean.startsWith(QStringLiteral("\\\\?"))
        || clean.startsWith(QStringLiteral("\\\\."))) {
        if (error) *error = QStringLiteral("Choose a relative file path inside the workspace.");
        return std::nullopt;
    }
    for (const auto& component : components) {
        if (component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral("..")) {
            if (error) *error = QStringLiteral("The path must not contain empty or parent components.");
            return std::nullopt;
        }
        if (isSensitivePathComponent(component) || isPrunedSearchDirectory(component)) {
            if (error) *error = QStringLiteral("Files in this secret-bearing path are not available.");
            return std::nullopt;
        }
    }
    const QString candidate = QDir(m_root).absoluteFilePath(clean);
    if (!isInsideWorkspace(candidate, false) || !isInsideWorkspace(candidate, true)) {
        if (error) *error = QStringLiteral("The path resolves outside the workspace.");
        return std::nullopt;
    }
    QString current = QDir::fromNativeSeparators(m_root);
    for (const QString& component : components) {
        current = QDir(current).filePath(component);
        const QFileInfo info(current);
        if ((info.isSymLink() || info.isJunction()) && !isInsideWorkspace(current, true)) {
            if (error) *error = QStringLiteral("The path resolves outside the workspace.");
            return std::nullopt;
        }
    }
    return candidate;
}

} // namespace devhub::application::files
