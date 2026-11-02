#pragma once

#include "TaskNoteLinkTypes.h"
#include "../../shared/common/Result.h"

#include <QVector>

namespace devhub::application::planner {

class ITaskNoteLinkRepository
{
public:
    virtual ~ITaskNoteLinkRepository() = default;
    [[nodiscard]] virtual shared::common::Result<void> requireActiveWorkspace(const QString& workspaceId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> add(const QString& workspaceId,
        const QString& taskId, const QString& noteId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> remove(const QString& workspaceId,
        const QString& taskId, const QString& noteId) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<TaskNoteLink>> listForTask(
        const QString& workspaceId, const QString& taskId) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<TaskNoteLink>> listForNote(
        const QString& workspaceId, const QString& noteId) = 0;
};

} // namespace devhub::application::planner
