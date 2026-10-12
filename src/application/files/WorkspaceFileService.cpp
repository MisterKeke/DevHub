#include "WorkspaceFileService.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QRegularExpression>
#include <algorithm>

namespace devhub::application::files {
namespace {
bool isPrunedName(const QString& name)
{
    return WorkspaceFilePolicy::isSensitivePathComponent(name)
        || WorkspaceFilePolicy::isPrunedSearchDirectory(name);
}
}

WorkspaceFileRead WorkspaceFileService::readPreview(const QString& relativePath) const
{
    WorkspaceFileRead result;
    QString error;
    const auto path = m_policy.resolveReadPath(relativePath, &error);
    if (!path) { result.message = error; return result; }
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly)) { result.message = QStringLiteral("Could not read this file."); return result; }
    const QFileInfo info(*path);
    result.byteCount = info.size();
    if (result.byteCount > PreviewByteLimit) { result.tooLarge = true; result.message = QStringLiteral("Preview unavailable: file is larger than 1 MiB."); return result; }
    QByteArray bytes = file.read(PreviewByteLimit + 1);
    if (bytes.size() > PreviewByteLimit) { result.tooLarge = true; result.message = QStringLiteral("Preview unavailable: file is larger than 1 MiB."); return result; }
    if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) bytes.remove(0, 3);
    if (bytes.contains('\0')) { result.binary = true; result.message = QStringLiteral("Preview unavailable: this looks like a binary file."); return result; }
    result.text = QString::fromUtf8(bytes.constData(), bytes.size());
    if (result.text.toUtf8() != bytes) { result.text.clear(); result.message = QStringLiteral("Preview unavailable: this file is not UTF-8 or ASCII."); return result; }
    result.readable = true;
    return result;
}

WorkspaceFileRange WorkspaceFileService::readRange(const QString& relativePath, qint64 offset, qint64 length, qint64 maximumReturnedBytes) const
{
    WorkspaceFileRange result;
    if (offset < 0 || length < 0 || maximumReturnedBytes < 0) return result;
    const WorkspaceFileRead preview = readPreview(relativePath);
    if (!preview.readable) return result;
    result.readable = true;
    const QByteArray utf8 = preview.text.toUtf8();
    if (offset >= utf8.size()) return result;
    const qint64 byteCap = std::min<qint64>(maximumReturnedBytes, 64 * 1024);
    qsizetype begin = static_cast<qsizetype>(offset);
    qsizetype end = begin + static_cast<qsizetype>(std::min({length, byteCap, static_cast<qint64>(utf8.size()) - offset}));
    const auto continuation = [&utf8](qsizetype position) {
        return position < utf8.size() && (static_cast<unsigned char>(utf8.at(position)) & 0xC0) == 0x80;
    };
    while (begin < end && continuation(begin)) ++begin;
    while (end > begin && continuation(end)) --end;
    result.bytes = utf8.mid(begin, end - begin);
    result.truncated = begin != offset || end < utf8.size();
    return result;
}

WorkspaceFileSearchResult WorkspaceFileService::searchNames(const QString& query,
    const std::shared_ptr<std::atomic_bool>& cancellation, WorkspaceFileSearchLimits limits) const
{
    WorkspaceFileSearchResult result;
    limits.maximumDepth = qMin(limits.maximumDepth, 12);
    limits.maximumEntries = qMin(limits.maximumEntries, 10000);
    limits.maximumMilliseconds = qMin(limits.maximumMilliseconds, 2000);
    limits.maximumResults = qMin(limits.maximumResults, 100);
    const QString needle = query.trimmed();
    if (needle.isEmpty() || limits.maximumDepth < 0 || limits.maximumEntries < 1
        || limits.maximumMilliseconds < 1 || limits.maximumResults < 1 || m_policy.workspaceRoot().isEmpty()) return result;
    QElapsedTimer timer; timer.start();
    struct Pending { QString path; int depth; };
    QList<Pending> pending{{m_policy.workspaceRoot(), 0}};
    while (!pending.isEmpty()) {
        if (cancellation && cancellation->load()) { result.cancelled = true; break; }
        if (timer.elapsed() >= limits.maximumMilliseconds || result.entriesVisited >= limits.maximumEntries) { result.budgetExhausted = true; break; }
        const Pending current = pending.takeLast();
        QDirIterator iterator(current.path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (iterator.hasNext()) {
            if (cancellation && cancellation->load()) { result.cancelled = true; break; }
            if (timer.elapsed() >= limits.maximumMilliseconds || result.entriesVisited >= limits.maximumEntries) { result.budgetExhausted = true; break; }
            const QFileInfo info = iterator.nextFileInfo();
            ++result.entriesVisited;
            if (info.isSymLink() || info.isJunction()) continue;
            const QString name = info.fileName();
            if (isPrunedName(name)) continue;
            const QString relative = m_policy.relativePath(info.absoluteFilePath());
            if (name.contains(needle, Qt::CaseInsensitive) || relative.contains(needle, Qt::CaseInsensitive)) {
                if (!relative.startsWith(QStringLiteral("..")) && !relative.contains(QLatin1Char(':'))) result.relativePaths.append(relative);
                if (result.relativePaths.size() >= limits.maximumResults) { result.budgetExhausted = !pending.isEmpty() || result.entriesVisited < limits.maximumEntries; return result; }
            }
            if (info.isDir() && !info.isSymLink() && !info.isJunction() && current.depth < limits.maximumDepth && !isPrunedName(name))
                pending.append({info.absoluteFilePath(), current.depth + 1});
        }
        if (result.cancelled || result.budgetExhausted) break;
    }
    return result;
}

} // namespace devhub::application::files
