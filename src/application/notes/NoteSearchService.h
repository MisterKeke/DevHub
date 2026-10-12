#pragma once

#include "../files/WorkspaceFilePolicy.h"
#include <QStringList>
#include <atomic>
#include <memory>

namespace devhub::application::notes {

struct AuthorizedNoteSource final { QString noteId; QString path; };
struct NoteSearchResult final { QStringList noteIds; bool cancelled = false; int filesRead = 0; };

// Pure bounded scan over a repository-authorized value snapshot. This service
// never owns or receives the GUI repository or its SQL connection.
class NoteSearchService final
{
public:
    static constexpr qint64 SourceByteLimit = 2 * 1024 * 1024;
    [[nodiscard]] NoteSearchResult search(const QList<AuthorizedNoteSource>& sources, const QString& needle,
        const std::shared_ptr<std::atomic_bool>& cancellation = {}, int maximumResults = 500) const;
};

} // namespace devhub::application::notes
