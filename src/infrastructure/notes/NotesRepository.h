#pragma once

#include "../../application/notes/INotesRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../domain/account/AccountId.h"

namespace devhub::infrastructure::notes {

class NotesRepository final : public application::notes::INotesRepository
{
public:
    NotesRepository(database::InternalDatabaseManager& database, domain::account::AccountId accountId, QString notesRoot);
    [[nodiscard]] shared::common::Result<QVector<application::notes::NoteNode>> list(const QString& workspaceId, bool includeTrash) override;
    [[nodiscard]] shared::common::Result<application::notes::NoteNode> create(const QString& workspaceId, QString parentId,
        QString title, application::notes::NoteNodeKind kind) override;
    [[nodiscard]] shared::common::Result<void> rename(const QString& workspaceId, const QString& id, QString title) override;
    [[nodiscard]] shared::common::Result<void> move(const QString& workspaceId, const QString& id, QString parentId) override;
    [[nodiscard]] shared::common::Result<void> setPinned(const QString& workspaceId, const QString& id, bool pinned) override;
    [[nodiscard]] shared::common::Result<void> setTags(const QString& workspaceId, const QString& id, QStringList tags) override;
    [[nodiscard]] shared::common::Result<QString> readContent(const QString& workspaceId, const QString& id) override;
    [[nodiscard]] shared::common::Result<QString> saveContent(const QString& workspaceId, const QString& id,
        const QString& content, const QString& expectedHash) override;
    [[nodiscard]] QString contentPath(const QString& workspaceId, const QString& id) const override;
    [[nodiscard]] shared::common::Result<void> setTrashed(const QString& workspaceId, const QString& id, bool trashed) override;
    [[nodiscard]] shared::common::Result<void> permanentlyRemove(const QString& workspaceId, const QString& id) override;
private:
    [[nodiscard]] bool ownsNode(const QString& workspaceId, const QString& id) const;
    database::InternalDatabaseManager& m_database;
    domain::account::AccountId m_accountId;
    QString m_notesRoot;
};

} // namespace devhub::infrastructure::notes
