#pragma once

#include "ITaskNoteLinkRepository.h"
#include "PlannerService.h"
#include "../notes/INotesRepository.h"

namespace devhub::application::planner {

class TaskNoteLinkService final
{
public:
    TaskNoteLinkService(ITaskNoteLinkRepository& links, PlannerService& planner,
                        notes::INotesRepository& notes)
        : m_links(links), m_planner(planner), m_notes(notes) {}

    [[nodiscard]] shared::common::Result<void> link(const QString& workspaceId,
        const QString& taskId, const QString& noteId);
    [[nodiscard]] shared::common::Result<void> unlink(const QString& workspaceId,
        const QString& taskId, const QString& noteId);
    [[nodiscard]] shared::common::Result<QVector<LinkedNote>> linkedNotesForTask(
        const QString& workspaceId, const QString& taskId);
    [[nodiscard]] shared::common::Result<QVector<LinkedPlannerTask>> linkedTasksForNote(
        const QString& workspaceId, const QString& noteId);
    [[nodiscard]] shared::common::Result<QVector<LinkedNote>> searchNotes(
        const QString& workspaceId, const QString& taskId, const QString& query, int limit = 100);

private:
    [[nodiscard]] shared::common::Result<void> requireActiveWorkspace(const QString& workspaceId);
    [[nodiscard]] shared::common::Result<notes::NoteNode> noteInWorkspace(
        const QString& workspaceId, const QString& noteId, bool includeTrash);

    ITaskNoteLinkRepository& m_links;
    PlannerService& m_planner;
    notes::INotesRepository& m_notes;
};

} // namespace devhub::application::planner
