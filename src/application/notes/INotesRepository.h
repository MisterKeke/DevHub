#pragma once

#include "NoteTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::notes {

class INotesRepository
{
public:
    virtual ~INotesRepository() = default;
    [[nodiscard]] virtual shared::common::Result<QVector<NoteNode>> list(const QString& workspaceId, bool includeTrash) = 0;
    [[nodiscard]] virtual shared::common::Result<NoteNode> create(const QString& workspaceId, QString parentId,
                                                                   QString title, NoteNodeKind kind) = 0;
    [[nodiscard]] virtual shared::common::Result<void> rename(const QString& workspaceId, const QString& id, QString title) = 0;
    [[nodiscard]] virtual shared::common::Result<void> move(const QString& workspaceId, const QString& id, QString parentId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> setPinned(const QString& workspaceId, const QString& id, bool pinned) = 0;
    [[nodiscard]] virtual shared::common::Result<void> setTags(const QString& workspaceId, const QString& id, QStringList tags) = 0;
    [[nodiscard]] virtual shared::common::Result<QString> readContent(const QString& workspaceId, const QString& id) = 0;
    [[nodiscard]] virtual shared::common::Result<QString> saveContent(const QString& workspaceId, const QString& id,
                                                                       const QString& content, const QString& expectedHash) = 0;
    [[nodiscard]] virtual QString contentPath(const QString& workspaceId, const QString& id) const = 0;
    [[nodiscard]] virtual shared::common::Result<void> setTrashed(const QString& workspaceId, const QString& id, bool trashed) = 0;
    [[nodiscard]] virtual shared::common::Result<void> permanentlyRemove(const QString& workspaceId, const QString& id) = 0;
};

} // namespace devhub::application::notes
