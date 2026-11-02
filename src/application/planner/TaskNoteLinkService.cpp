#include "TaskNoteLinkService.h"

#include "../../domain/errors/AppError.h"
#include "../../domain/planner/PlannerTypes.h"

#include <QHash>
#include <QSet>
#include <algorithm>

namespace devhub::application::planner {
namespace {
using notes::NoteNode;
using notes::NoteNodeKind;

template<typename T>
shared::common::Result<T> unavailable(const QString& message, const QString& operation)
{
    return shared::common::Result<T>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
        message, {}, operation));
}

QString folderPath(const NoteNode& note, const QHash<QString, NoteNode>& byId)
{
    QStringList parts;
    QString parentId = note.parentId;
    while (!parentId.isEmpty() && byId.contains(parentId)) {
        const NoteNode parent = byId.value(parentId);
        if (parent.kind != NoteNodeKind::Folder) break;
        parts.prepend(parent.title);
        parentId = parent.parentId;
    }
    return parts.join(QStringLiteral(" / "));
}
}

shared::common::Result<void> TaskNoteLinkService::requireActiveWorkspace(const QString& workspaceId)
{
    return m_links.requireActiveWorkspace(workspaceId);
}

shared::common::Result<notes::NoteNode> TaskNoteLinkService::noteInWorkspace(
    const QString& workspaceId, const QString& noteId, bool includeTrash)
{
    auto listed = m_notes.list(workspaceId, includeTrash);
    if (!listed) return shared::common::Result<notes::NoteNode>::failure(listed.error());
    const auto found = std::find_if(listed.value().cbegin(), listed.value().cend(),
        [&noteId](const NoteNode& node) { return node.id == noteId && node.kind == NoteNodeKind::Note; });
    if (found == listed.value().cend())
        return unavailable<NoteNode>(QStringLiteral("The note is no longer available in this workspace."),
                                     QStringLiteral("validate task note link"));
    return shared::common::Result<NoteNode>::success(*found);
}

shared::common::Result<void> TaskNoteLinkService::link(const QString& workspaceId,
    const QString& taskId, const QString& noteId)
{
    auto workspace = requireActiveWorkspace(workspaceId);
    if (!workspace) return workspace;
    auto task = m_planner.getTask(workspaceId, taskId);
    if (!task) return shared::common::Result<void>::failure(task.error());
    auto note = noteInWorkspace(workspaceId, noteId, true);
    if (!note) return shared::common::Result<void>::failure(note.error());
    if (note.value().trashed)
        return unavailable<void>(QStringLiteral("Restore the note before linking it to a task."),
                                 QStringLiteral("link note to Planner task"));
    return m_links.add(workspaceId, taskId, noteId);
}

shared::common::Result<void> TaskNoteLinkService::unlink(const QString& workspaceId,
    const QString& taskId, const QString& noteId)
{
    auto workspace = requireActiveWorkspace(workspaceId);
    if (!workspace) return workspace;
    auto task = m_planner.getTask(workspaceId, taskId);
    if (!task) return shared::common::Result<void>::failure(task.error());
    auto note = noteInWorkspace(workspaceId, noteId, true);
    if (!note) return shared::common::Result<void>::failure(note.error());
    return m_links.remove(workspaceId, taskId, noteId);
}

shared::common::Result<QVector<LinkedNote>> TaskNoteLinkService::linkedNotesForTask(
    const QString& workspaceId, const QString& taskId)
{
    auto workspace = requireActiveWorkspace(workspaceId);
    if (!workspace) return shared::common::Result<QVector<LinkedNote>>::failure(workspace.error());
    auto task = m_planner.getTask(workspaceId, taskId);
    if (!task) return shared::common::Result<QVector<LinkedNote>>::failure(task.error());
    auto links = m_links.listForTask(workspaceId, taskId);
    if (!links) return shared::common::Result<QVector<LinkedNote>>::failure(links.error());
    auto listed = m_notes.list(workspaceId, true);
    if (!listed) return shared::common::Result<QVector<LinkedNote>>::failure(listed.error());

    QHash<QString, NoteNode> byId;
    for (const NoteNode& node : listed.value()) byId.insert(node.id, node);
    QVector<LinkedNote> result;
    result.reserve(links.value().size());
    for (const TaskNoteLink& link : links.value()) {
        const auto it = byId.constFind(link.noteId);
        if (it == byId.cend() || it->kind != NoteNodeKind::Note) continue;
        result.append({it->id, it->title, folderPath(*it, byId), it->tags, it->trashed, true});
    }
    return shared::common::Result<QVector<LinkedNote>>::success(std::move(result));
}

shared::common::Result<QVector<LinkedPlannerTask>> TaskNoteLinkService::linkedTasksForNote(
    const QString& workspaceId, const QString& noteId)
{
    auto workspace = requireActiveWorkspace(workspaceId);
    if (!workspace) return shared::common::Result<QVector<LinkedPlannerTask>>::failure(workspace.error());
    auto note = noteInWorkspace(workspaceId, noteId, true);
    if (!note) return shared::common::Result<QVector<LinkedPlannerTask>>::failure(note.error());
    auto links = m_links.listForNote(workspaceId, noteId);
    if (!links) return shared::common::Result<QVector<LinkedPlannerTask>>::failure(links.error());

    QVector<LinkedPlannerTask> result;
    result.reserve(links.value().size());
    for (const TaskNoteLink& link : links.value()) {
        auto task = m_planner.getTask(workspaceId, link.taskId);
        if (!task) continue;
        result.append({task.value().id, task.value().title,
            domain::planner::plannerStatusName(task.value().status), task.value().priority});
    }
    return shared::common::Result<QVector<LinkedPlannerTask>>::success(std::move(result));
}

shared::common::Result<QVector<LinkedNote>> TaskNoteLinkService::searchNotes(
    const QString& workspaceId, const QString& taskId, const QString& query, int limit)
{
    auto workspace = requireActiveWorkspace(workspaceId);
    if (!workspace) return shared::common::Result<QVector<LinkedNote>>::failure(workspace.error());
    auto task = m_planner.getTask(workspaceId, taskId);
    if (!task) return shared::common::Result<QVector<LinkedNote>>::failure(task.error());
    auto notes = m_notes.list(workspaceId, true);
    if (!notes) return shared::common::Result<QVector<LinkedNote>>::failure(notes.error());
    auto links = m_links.listForTask(workspaceId, taskId);
    if (!links) return shared::common::Result<QVector<LinkedNote>>::failure(links.error());

    QSet<QString> linkedIds;
    for (const TaskNoteLink& link : links.value()) linkedIds.insert(link.noteId);
    QHash<QString, NoteNode> byId;
    for (const NoteNode& node : notes.value()) byId.insert(node.id, node);
    const QString needle = query.trimmed();
    QVector<LinkedNote> result;
    result.reserve(qMin<qsizetype>(qBound(1, limit, 200), notes.value().size()));
    for (const NoteNode& node : notes.value()) {
        if (node.kind != NoteNodeKind::Note || node.trashed) continue;
        const QString path = folderPath(node, byId);
        const bool matches = needle.isEmpty() || node.title.contains(needle, Qt::CaseInsensitive)
            || path.contains(needle, Qt::CaseInsensitive)
            || std::any_of(node.tags.cbegin(), node.tags.cend(), [&needle](const QString& tag) {
                return tag.contains(needle, Qt::CaseInsensitive);
            });
        if (!matches) continue;
        result.append({node.id, node.title, path, node.tags, false, linkedIds.contains(node.id)});
        if (result.size() >= qBound(1, limit, 200)) break;
    }
    return shared::common::Result<QVector<LinkedNote>>::success(std::move(result));
}

} // namespace devhub::application::planner
