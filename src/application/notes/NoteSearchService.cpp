#include "NoteSearchService.h"

#include <QFile>
#include <QFileInfo>

namespace devhub::application::notes {
namespace {
QByteArray readBounded(QFile& file, qint64 limit, bool* truncated)
{
    QByteArray bytes = file.read(limit + 1);
    if (truncated) *truncated = bytes.size() > limit;
    if (bytes.size() > limit) bytes.truncate(static_cast<int>(limit));
    return bytes;
}
}

NoteSearchResult NoteSearchService::search(const QList<AuthorizedNoteSource>& sources, const QString& needle,
    const std::shared_ptr<std::atomic_bool>& cancellation, int maximumResults) const
{
    NoteSearchResult result;
    if (needle.trimmed().isEmpty() || maximumResults < 1) return result;
    for (const auto& source : sources) {
        if (cancellation && cancellation->load()) { result.cancelled = true; break; }
        if (result.noteIds.size() >= maximumResults) break;
        const QFileInfo info(source.path);
        if (!info.exists() || !info.isFile() || files::WorkspaceFilePolicy::hasLinkComponent(source.path)
            || info.size() > SourceByteLimit) continue;
        QFile file(source.path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        bool truncated = false;
        QByteArray bytes = readBounded(file, SourceByteLimit, &truncated);
        if (truncated || bytes.contains('\0')) continue;
        if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) bytes.remove(0, 3);
        const QString text = QString::fromUtf8(bytes.constData(), bytes.size());
        if (text.toUtf8() != bytes) continue;
        ++result.filesRead;
        if (text.contains(needle, Qt::CaseInsensitive)) result.noteIds.append(source.noteId);
    }
    return result;
}

} // namespace devhub::application::notes
