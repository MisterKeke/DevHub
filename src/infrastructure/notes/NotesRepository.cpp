#include "NotesRepository.h"

#include "../database/SqlErrorMapper.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlQuery>
#include <QUuid>
#include <QUuid>

namespace devhub::infrastructure::notes {
using application::notes::NoteNode;
using application::notes::NoteNodeKind;
namespace {
using devhub::shared::common::Result;
using devhub::domain::errors::AppError;

template<class T>
Result<T> noteFailure(const QString& userMessage, const QString& detail, const QString& operation)
{
    return Result<T>::failure(AppError::failure(devhub::domain::errors::ErrorCode::DatabaseQueryFailed,
        devhub::domain::errors::ErrorCategory::FileSystem, userMessage, detail, operation));
}
QString hashContent(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
}

NotesRepository::NotesRepository(database::InternalDatabaseManager& database, domain::account::AccountId accountId, QString notesRoot)
    : m_database(database), m_accountId(std::move(accountId)), m_notesRoot(std::move(notesRoot)) {}

bool NotesRepository::ownsNode(const QString& workspaceId, const QString& id) const
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT 1 FROM note_nodes n JOIN workspaces w ON w.id=n.workspace_id "
        "WHERE n.id=? AND n.workspace_id=? AND w.account_id=?"));
    query.addBindValue(id); query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    return query.exec() && query.next();
}

Result<QVector<NoteNode>> NotesRepository::list(const QString& workspaceId, bool includeTrash)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT n.id,n.workspace_id,COALESCE(n.parent_id,''),n.title,n.node_type,n.sort_order,"
        "n.is_pinned,n.is_trashed,n.content_hash,COALESCE(GROUP_CONCAT(t.name, char(31)), '') "
        "FROM note_nodes n JOIN workspaces w ON w.id=n.workspace_id "
        "LEFT JOIN note_tag_links l ON l.note_id=n.id LEFT JOIN note_tags t ON t.id=l.tag_id "
        "WHERE n.workspace_id=? AND w.account_id=? AND (? OR n.is_trashed=0) "
        "GROUP BY n.id ORDER BY n.is_trashed,n.is_pinned DESC,n.sort_order,n.title COLLATE NOCASE"));
    query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString()); query.addBindValue(includeTrash ? 1 : 0);
    if (!query.exec()) return Result<QVector<NoteNode>>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("list notes")));
    QVector<NoteNode> nodes;
    while (query.next()) {
        NoteNode node{query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
            query.value(3).toString(), query.value(4).toString() == QStringLiteral("folder") ? NoteNodeKind::Folder : NoteNodeKind::Note,
            query.value(5).toInt(), query.value(6).toBool(), query.value(7).toBool(), query.value(8).toString(), {}};
        const QString tags = query.value(9).toString();
        if (!tags.isEmpty()) node.tags = tags.split(QChar(31), Qt::SkipEmptyParts);
        nodes.append(std::move(node));
    }
    return Result<QVector<NoteNode>>::success(std::move(nodes));
}

Result<NoteNode> NotesRepository::create(const QString& workspaceId, QString parentId, QString title, NoteNodeKind kind)
{
    title = title.trimmed();
    if (title.isEmpty() || title.size() > 120 || title.contains(QLatin1Char('/')) || title.contains(QLatin1Char('\\')))
        return noteFailure<NoteNode>(QStringLiteral("Enter a title of at most 120 characters without path separators."), title, QStringLiteral("create note"));
    if (!parentId.isEmpty()) {
        QSqlQuery parent(m_database.database());
        parent.prepare(QStringLiteral("SELECT 1 FROM note_nodes n JOIN workspaces w ON w.id=n.workspace_id "
            "WHERE n.id=? AND n.workspace_id=? AND n.node_type='folder' AND n.is_trashed=0 AND w.account_id=?"));
        parent.addBindValue(parentId); parent.addBindValue(workspaceId); parent.addBindValue(m_accountId.toString());
        if (!parent.exec() || !parent.next()) return noteFailure<NoteNode>(QStringLiteral("Choose an existing notes folder."), parent.lastError().text(), QStringLiteral("create note"));
        parent.finish();
    }
    NoteNode node{QUuid::createUuid().toString(QUuid::WithoutBraces), workspaceId, parentId, title, kind, 0, false, false, {}, {}};
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO note_nodes(id,workspace_id,parent_id,title,node_type,content_hash) "
        "SELECT ?,?,?,?,?,'' WHERE EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=?)"));
    query.addBindValue(node.id); query.addBindValue(workspaceId); query.addBindValue(parentId.isEmpty() ? QVariant() : QVariant(parentId));
    query.addBindValue(title); query.addBindValue(kind == NoteNodeKind::Folder ? QStringLiteral("folder") : QStringLiteral("note"));
    query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return Result<NoteNode>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("create note metadata")));
    if (query.numRowsAffected() != 1) return noteFailure<NoteNode>(QStringLiteral("Choose an active workspace."), workspaceId, QStringLiteral("create note metadata"));
    if (kind == NoteNodeKind::Note) {
        const QString path = contentPath(workspaceId, node.id);
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            QSqlQuery remove(m_database.database()); remove.prepare(QStringLiteral("DELETE FROM note_nodes WHERE id=? AND workspace_id=?"));
            remove.addBindValue(node.id); remove.addBindValue(workspaceId); remove.exec();
            return noteFailure<NoteNode>(QStringLiteral("DevHub could not prepare the notes folder."), path, QStringLiteral("create note file"));
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !file.commit()) {
            const QString error = file.errorString();
            QSqlQuery remove(m_database.database()); remove.prepare(QStringLiteral("DELETE FROM note_nodes WHERE id=? AND workspace_id=?"));
            remove.addBindValue(node.id); remove.addBindValue(workspaceId); remove.exec();
            return noteFailure<NoteNode>(QStringLiteral("DevHub could not create the note file."), error, QStringLiteral("create note file"));
        }
        node.contentHash = hashContent({});
        QSqlQuery update(m_database.database()); update.prepare(QStringLiteral("UPDATE note_nodes SET content_hash=? WHERE id=? AND workspace_id=?"));
        update.addBindValue(node.contentHash); update.addBindValue(node.id); update.addBindValue(workspaceId);
        if (!update.exec()) return Result<NoteNode>::failure(database::SqlErrorMapper::fromSqlError(update.lastError(), QStringLiteral("initialize note hash")));
    }
    return Result<NoteNode>::success(std::move(node));
}

Result<void> NotesRepository::rename(const QString& workspaceId, const QString& id, QString title)
{
    title = title.trimmed();
    if (title.isEmpty() || title.size() > 120 || title.contains(QLatin1Char('/')) || title.contains(QLatin1Char('\\')))
        return noteFailure<void>(QStringLiteral("Enter a title of at most 120 characters without path separators."), title, QStringLiteral("rename note"));
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("UPDATE note_nodes SET title=?,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') "
        "WHERE id=? AND workspace_id=? AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(title); query.addBindValue(id); query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("rename note")));
    return query.numRowsAffected() == 1 ? Result<void>::success() : noteFailure<void>(QStringLiteral("The note could not be found."), id, QStringLiteral("rename note"));
}

Result<void> NotesRepository::move(const QString& workspaceId, const QString& id, QString parentId)
{
    if (id == parentId) return noteFailure<void>(QStringLiteral("A folder cannot be moved inside itself."), id, QStringLiteral("move note"));
    if (!parentId.isEmpty()) {
        QSqlQuery parent(m_database.database()); parent.prepare(QStringLiteral("SELECT 1 FROM note_nodes p JOIN workspaces w ON w.id=p.workspace_id "
            "WHERE p.id=? AND p.workspace_id=? AND p.node_type='folder' AND p.is_trashed=0 AND w.account_id=?"));
        parent.addBindValue(parentId); parent.addBindValue(workspaceId); parent.addBindValue(m_accountId.toString());
        if (!parent.exec() || !parent.next()) return noteFailure<void>(QStringLiteral("Choose a destination folder."), parent.lastError().text(), QStringLiteral("move note"));
        parent.finish();
        QSqlQuery cycle(m_database.database()); cycle.prepare(QStringLiteral("WITH RECURSIVE descendants(id) AS (SELECT id FROM note_nodes WHERE parent_id=? AND workspace_id=? UNION ALL SELECT n.id FROM note_nodes n JOIN descendants d ON n.parent_id=d.id WHERE n.workspace_id=?) SELECT 1 FROM descendants WHERE id=?"));
        cycle.addBindValue(id); cycle.addBindValue(workspaceId); cycle.addBindValue(workspaceId); cycle.addBindValue(parentId);
        if (!cycle.exec()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(cycle.lastError(), QStringLiteral("validate note folder move")));
        if (cycle.next()) return noteFailure<void>(QStringLiteral("A folder cannot be moved inside its own contents."), id, QStringLiteral("move note"));
        cycle.finish();
    }
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("UPDATE note_nodes SET parent_id=?,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') "
        "WHERE id=? AND workspace_id=? AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(parentId.isEmpty() ? QVariant() : QVariant(parentId)); query.addBindValue(id); query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("move note")));
    return query.numRowsAffected() == 1 ? Result<void>::success() : noteFailure<void>(QStringLiteral("The note could not be found."), id, QStringLiteral("move note"));
}

Result<void> NotesRepository::setPinned(const QString& workspaceId, const QString& id, bool pinned)
{
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("UPDATE note_nodes SET is_pinned=?,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND workspace_id=? AND node_type='note' AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(pinned ? 1 : 0); query.addBindValue(id); query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("pin note")));
    return query.numRowsAffected() == 1 ? Result<void>::success() : noteFailure<void>(QStringLiteral("The note could not be found."), id, QStringLiteral("pin note"));
}

Result<void> NotesRepository::setTags(const QString& workspaceId, const QString& id, QStringList tags)
{
    if (!ownsNode(workspaceId, id)) return noteFailure<void>(QStringLiteral("The note could not be found."), id, QStringLiteral("tag note"));
    QSqlQuery type(m_database.database()); type.prepare(QStringLiteral("SELECT node_type FROM note_nodes WHERE id=? AND workspace_id=?"));
    type.addBindValue(id); type.addBindValue(workspaceId); type.exec(); if (!type.next() || type.value(0).toString() != QStringLiteral("note")) return noteFailure<void>(QStringLiteral("Tags can be added to notes."), id, QStringLiteral("tag note"));
    type.finish();
    QStringList normalizedTags;
    for (const QString& sourceTag : tags) {
        const QString tag = sourceTag.trimmed();
        if (!tag.isEmpty() && !normalizedTags.contains(tag)) normalizedTags.append(tag);
    }
    tags = std::move(normalizedTags);
    if (!m_database.database().transaction()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(m_database.database().lastError(), QStringLiteral("tag note")));
    for (QString tag : tags) {
        tag = tag.trimmed(); if (tag.isEmpty()) continue;
        if (tag.size() > 48) { m_database.database().rollback(); return noteFailure<void>(QStringLiteral("Tags can contain at most 48 characters."), tag, QStringLiteral("tag note")); }
        QSqlQuery insert(m_database.database()); insert.prepare(QStringLiteral("INSERT OR IGNORE INTO note_tags(id,workspace_id,name) SELECT ?,?,? WHERE EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=?)"));
        insert.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces)); insert.addBindValue(workspaceId); insert.addBindValue(tag); insert.addBindValue(workspaceId); insert.addBindValue(m_accountId.toString());
        if (!insert.exec()) { m_database.database().rollback(); return Result<void>::failure(database::SqlErrorMapper::fromSqlError(insert.lastError(), QStringLiteral("create note tag"))); }
        QSqlQuery link(m_database.database()); link.prepare(QStringLiteral("INSERT OR IGNORE INTO note_tag_links(note_id,tag_id) SELECT ?,id FROM note_tags WHERE workspace_id=? AND name=?"));
        link.addBindValue(id); link.addBindValue(workspaceId); link.addBindValue(tag);
        if (!link.exec()) { m_database.database().rollback(); return Result<void>::failure(database::SqlErrorMapper::fromSqlError(link.lastError(), QStringLiteral("link note tag"))); }
    }
    // Replace links with the submitted set using prepared per-tag operations; empty means clear all.
    if (tags.isEmpty()) { QSqlQuery deleteLinks(m_database.database()); deleteLinks.prepare(QStringLiteral("DELETE FROM note_tag_links WHERE note_id=?")); deleteLinks.addBindValue(id); deleteLinks.exec(); }
    else {
        QStringList placeholders; for (qsizetype i = 0; i < tags.size(); ++i) placeholders.append(QStringLiteral("?"));
        QSqlQuery deleteLinks(m_database.database()); deleteLinks.prepare(QStringLiteral("DELETE FROM note_tag_links WHERE note_id=? AND tag_id NOT IN(SELECT id FROM note_tags WHERE workspace_id=? AND name IN(%1))").arg(placeholders.join(QLatin1Char(','))));
        deleteLinks.addBindValue(id); deleteLinks.addBindValue(workspaceId); for (const auto& tag : tags) deleteLinks.addBindValue(tag.trimmed());
        if (!deleteLinks.exec()) { m_database.database().rollback(); return Result<void>::failure(database::SqlErrorMapper::fromSqlError(deleteLinks.lastError(), QStringLiteral("replace note tags"))); }
    }
    if (!m_database.database().commit()) { const auto error = m_database.database().lastError(); m_database.database().rollback(); return Result<void>::failure(database::SqlErrorMapper::fromSqlError(error, QStringLiteral("commit note tags"))); }
    return Result<void>::success();
}

QString NotesRepository::contentPath(const QString& workspaceId, const QString& id) const
{
    if (QUuid::fromString(workspaceId).isNull() || QUuid::fromString(id).isNull()) return {};
    return QDir(QDir(m_notesRoot).filePath(m_accountId.toString())).filePath(workspaceId + QLatin1Char('/') + id + QStringLiteral(".md"));
}

Result<QString> NotesRepository::readContent(const QString& workspaceId, const QString& id)
{
    if (!ownsNode(workspaceId, id)) return noteFailure<QString>(QStringLiteral("The note could not be found."), id, QStringLiteral("read note"));
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("SELECT node_type FROM note_nodes WHERE id=? AND workspace_id=?")); query.addBindValue(id); query.addBindValue(workspaceId);
    if (!query.exec() || !query.next() || query.value(0).toString() != QStringLiteral("note")) return noteFailure<QString>(QStringLiteral("The note could not be opened."), id, QStringLiteral("read note"));
    QFile file(contentPath(workspaceId, id));
    if (!file.exists()) return Result<QString>::success({});
    if (!file.open(QIODevice::ReadOnly)) return noteFailure<QString>(QStringLiteral("DevHub could not read this note."), file.errorString(), QStringLiteral("read note"));
    const QByteArray bytes = file.readAll();
    return Result<QString>::success(QString::fromUtf8(bytes));
}

Result<QString> NotesRepository::saveContent(const QString& workspaceId, const QString& id, const QString& content, const QString& expectedHash)
{
    if (!ownsNode(workspaceId, id)) return noteFailure<QString>(QStringLiteral("The note could not be found."), id, QStringLiteral("save note"));
    const QString path = contentPath(workspaceId, id);
    QFile current(path);
    QByteArray currentBytes;
    if (current.exists()) {
        if (!current.open(QIODevice::ReadOnly)) return noteFailure<QString>(QStringLiteral("DevHub could not check the note for outside changes."), current.errorString(), QStringLiteral("save note"));
        currentBytes = current.readAll();
        current.close();
    }
    if (!expectedHash.isEmpty() && hashContent(currentBytes) != expectedHash)
        return noteFailure<QString>(QStringLiteral("This note changed outside DevHub. Your pending edits were kept; reload or compare before saving."), path, QStringLiteral("check note conflict"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return noteFailure<QString>(QStringLiteral("DevHub could not save the note."), file.errorString(), QStringLiteral("save note"));
    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) return noteFailure<QString>(QStringLiteral("DevHub could not save the note."), file.errorString(), QStringLiteral("save note"));
    const QString hash = hashContent(bytes);
    QSqlQuery update(m_database.database()); update.prepare(QStringLiteral("UPDATE note_nodes SET content_hash=?,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND workspace_id=? AND node_type='note' AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    update.addBindValue(hash); update.addBindValue(id); update.addBindValue(workspaceId); update.addBindValue(m_accountId.toString());
    if (!update.exec() || update.numRowsAffected() != 1) return noteFailure<QString>(QStringLiteral("The note file was saved, but its metadata could not be updated."), update.lastError().text(), QStringLiteral("save note metadata"));
    return Result<QString>::success(hash);
}

Result<void> NotesRepository::setTrashed(const QString& workspaceId, const QString& id, bool trashed)
{
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("WITH RECURSIVE descendants(id) AS (SELECT id FROM note_nodes WHERE id=? AND workspace_id=? UNION ALL SELECT n.id FROM note_nodes n JOIN descendants d ON n.parent_id=d.id WHERE n.workspace_id=?) "
        "UPDATE note_nodes SET is_trashed=?,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id IN(SELECT id FROM descendants) AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=?)"));
    query.addBindValue(id); query.addBindValue(workspaceId); query.addBindValue(workspaceId); query.addBindValue(trashed ? 1 : 0); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("move note to trash")));
    return query.numRowsAffected() > 0 ? Result<void>::success() : noteFailure<void>(QStringLiteral("The note could not be found."), id, QStringLiteral("move note to trash"));
}

Result<void> NotesRepository::permanentlyRemove(const QString& workspaceId, const QString& id)
{
    if (!ownsNode(workspaceId, id)) return noteFailure<void>(QStringLiteral("The note could not be found."), id, QStringLiteral("permanently remove note"));
    QSqlQuery files(m_database.database());
    files.prepare(QStringLiteral("WITH RECURSIVE descendants(id,node_type) AS (SELECT id,node_type FROM note_nodes WHERE id=? AND workspace_id=? UNION ALL SELECT n.id,n.node_type FROM note_nodes n JOIN descendants d ON n.parent_id=d.id WHERE n.workspace_id=?) SELECT id FROM descendants WHERE node_type='note'"));
    files.addBindValue(id); files.addBindValue(workspaceId); files.addBindValue(workspaceId);
    if (!files.exec()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(files.lastError(), QStringLiteral("list trashed note files")));
    QStringList noteIds; while (files.next()) noteIds.append(files.value(0).toString());
    files.finish();
    QSqlQuery query(m_database.database()); query.prepare(QStringLiteral("WITH RECURSIVE descendants(id) AS (SELECT id FROM note_nodes WHERE id=? AND workspace_id=? UNION ALL SELECT n.id FROM note_nodes n JOIN descendants d ON n.parent_id=d.id WHERE n.workspace_id=?) DELETE FROM note_nodes WHERE id IN(SELECT id FROM descendants)"));
    query.addBindValue(id); query.addBindValue(workspaceId); query.addBindValue(workspaceId);
    if (!query.exec()) return Result<void>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("remove note metadata")));
    for (const auto& noteId : noteIds) QFile::remove(contentPath(workspaceId, noteId));
    return Result<void>::success();
}

} // namespace devhub::infrastructure::notes
