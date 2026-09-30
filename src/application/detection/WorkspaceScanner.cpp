#include "WorkspaceScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <deque>

namespace devhub::application::detection {
namespace {
struct PendingDirectory final { QString absolutePath; QString relativePath; int depth = 0; };

QString comparisonPath(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

shared::common::Result<ScanReport> scanError(QString message, QString details)
{
    return shared::common::Result<ScanReport>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::PathUnavailable, domain::errors::ErrorCategory::Workspace,
        std::move(message), std::move(details), QStringLiteral("scan workspace")));
}

bool isIgnored(const QString& name, const ScanBudget& budget, const QVector<QRegularExpression>& patterns)
{
    const QString folded = name.toCaseFolded();
    if (budget.ignoredDirectoryNames.contains(folded)) return true;
    return std::any_of(patterns.cbegin(), patterns.cend(), [&](const auto& pattern) { return pattern.match(name).hasMatch(); });
}
}

shared::common::Result<ScanReport> WorkspaceScanner::scan(const QString& workspaceRoot,
    const ScanBudget& budget, CancellationCheck cancelled, ProgressReporter progress) const
{
    QFileInfo rootInfo(workspaceRoot);
    if (!rootInfo.exists() || !rootInfo.isDir() || !rootInfo.isReadable())
        return scanError(QStringLiteral("This workspace folder is no longer available."),
                         QStringLiteral("Workspace root does not exist or is not a directory: %1").arg(workspaceRoot));
    const QString root = rootInfo.canonicalFilePath();
    if (root.isEmpty()) return scanError(QStringLiteral("DevHub cannot access this workspace folder."),
                                          QStringLiteral("Workspace root did not resolve to a canonical directory."));

    ScanReport report;
    report.detectorVersion = m_registry.outputVersion();
    QElapsedTimer elapsed;
    elapsed.start();
    QElapsedTimer progressTimer;
    progressTimer.start();
    qint64 lastProgressFileCount = 0;
    const auto reportProgress = [&](bool force) {
        if (!progress || (!force && progressTimer.elapsed() < 150 && report.inspectedFiles - lastProgressFileCount < 500)) return;
        lastProgressFileCount = report.inspectedFiles;
        progressTimer.restart();
        progress(QStringLiteral("Scanning workspace (%1 files inspected)").arg(report.inspectedFiles), -1);
    };

    QVector<QRegularExpression> ignorePatterns;
    for (const QString& expression : budget.ignoredDirectoryPatterns)
        ignorePatterns.append(QRegularExpression(expression, QRegularExpression::CaseInsensitiveOption));

    std::deque<PendingDirectory> pending;
    pending.push_back({root, QStringLiteral("."), 0});
    QSet<QString> visited;
    visited.insert(comparisonPath(root));
    QHash<QString, DetectionResult> results;
    QSet<QString> evidenceKeys;
    bool stop = false;
    while (!pending.empty() && !stop) {
        if (cancelled && cancelled()) return scanError(QStringLiteral("Workspace scan was cancelled."), QStringLiteral("Cancellation requested."));
        if (elapsed.elapsed() >= budget.wallTime.count()) { report.limitReached = QStringLiteral("time"); break; }
        if (!QFileInfo(root).isDir() || !QFileInfo(root).isReadable())
            return scanError(QStringLiteral("This workspace folder is no longer available."),
                             QStringLiteral("Workspace root disappeared during the scan."));

        PendingDirectory current = std::move(pending.front());
        pending.pop_front();
        const QFileInfo currentInfo(current.absolutePath);
        if (!currentInfo.exists() || !currentInfo.isDir()) continue;
        const QString canonical = currentInfo.canonicalFilePath();
        if (canonical.isEmpty()) continue;
        ++report.visitedDirectories;

        QDirIterator iterator(current.absolutePath,
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDirIterator::NoIteratorFlags);
        while (iterator.hasNext()) {
            if (cancelled && cancelled()) return scanError(QStringLiteral("Workspace scan was cancelled."), QStringLiteral("Cancellation requested."));
            if (elapsed.elapsed() >= budget.wallTime.count()) { report.limitReached = QStringLiteral("time"); stop = true; break; }
            if (!QFileInfo(root).isDir() || !QFileInfo(root).isReadable())
                return scanError(QStringLiteral("This workspace folder is no longer available."),
                                 QStringLiteral("Workspace root disappeared during the scan."));
            iterator.next();
            QFileInfo info = iterator.fileInfo();
            if (!info.exists()) continue; // An entry can disappear between enumeration and inspection.
            if (info.isSymLink()) continue; // Avoid following links and Windows junctions outside the workspace.
            const bool isDirectory = info.isDir();
            if (!isDirectory) {
                if (report.inspectedFiles >= budget.maximumInspectedFiles) {
                    report.limitReached = QStringLiteral("files"); stop = true; break;
                }
                ++report.inspectedFiles;
            }
            QString relative = QDir(root).relativeFilePath(info.absoluteFilePath());
            relative = QDir::fromNativeSeparators(QDir::cleanPath(relative));
            DetectionEntry entry{info, info.fileName(), relative, current.relativePath, isDirectory, info.size()};
            const auto found = m_registry.inspect(entry, budget);
            for (const auto& evidence : found) {
                const QString key = evidence.relativeDirectory + QChar(0x1f) + evidence.detectorId.value()
                    + QChar(0x1f) + evidence.relativePath;
                if (evidenceKeys.contains(key)) continue;
                evidenceKeys.insert(key);
                auto it = results.find(evidence.relativeDirectory);
                if (it == results.end()) {
                    DetectionResult result; result.relativeDirectory = evidence.relativeDirectory;
                    result.confidence = evidence.confidence;
                    it = results.insert(evidence.relativeDirectory, std::move(result));
                }
                if (static_cast<int>(evidence.confidence) > static_cast<int>(it->confidence)) it->confidence = evidence.confidence;
                it->evidence.append(evidence);
            }
            if (isDirectory) {
                const QString name = info.fileName();
                if (isIgnored(name, budget, ignorePatterns)) continue;
                if (current.depth >= budget.maximumDepth) { report.limitReached = QStringLiteral("depth"); continue; }
                const QString childCanonical = info.canonicalFilePath();
                if (childCanonical.isEmpty()) continue;
                const QString childKey = comparisonPath(childCanonical);
                if (visited.contains(childKey)) continue;
                visited.insert(childKey);
                pending.push_back({info.absoluteFilePath(), relative, current.depth + 1});
            }
            reportProgress(false);
        }
    }
    if (!QFileInfo(root).isDir() || !QFileInfo(root).isReadable())
        return scanError(QStringLiteral("This workspace folder is no longer available."),
                         QStringLiteral("Workspace root disappeared or became unreadable before scan completion."));
    report.elapsedMilliseconds = elapsed.elapsed();
    reportProgress(true);
    report.results.reserve(results.size());
    for (auto it = results.begin(); it != results.end(); ++it) report.results.append(std::move(it.value()));
    std::sort(report.results.begin(), report.results.end(), [](const auto& a, const auto& b) {
        return a.relativeDirectory < b.relativeDirectory;
    });
    for (auto& result : report.results) {
        std::sort(result.evidence.begin(), result.evidence.end(), [](const auto& a, const auto& b) {
            if (a.relativePath != b.relativePath) return a.relativePath < b.relativePath;
            return a.detectorId.value() < b.detectorId.value();
        });
    }
    return shared::common::Result<ScanReport>::success(std::move(report));
}

} // namespace devhub::application::detection
