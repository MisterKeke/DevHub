#pragma once

#include "../../application/planner/ITaskNoteLinkRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../domain/account/AccountId.h"

namespace devhub::infrastructure::planner {

class SqlTaskNoteLinkRepository final : public application::planner::ITaskNoteLinkRepository
{
public:
    SqlTaskNoteLinkRepository(database::InternalDatabaseManager& database,
                              domain::account::AccountId accountId)
        : m_database(database), m_accountId(std::move(accountId)) {}

    [[nodiscard]] shared::common::Result<void> requireActiveWorkspace(const QString& workspaceId) override;
    [[nodiscard]] shared::common::Result<void> add(const QString& workspaceId,
        const QString& taskId, const QString& noteId) override;
    [[nodiscard]] shared::common::Result<void> remove(const QString& workspaceId,
        const QString& taskId, const QString& noteId) override;
    [[nodiscard]] shared::common::Result<QVector<application::planner::TaskNoteLink>>
        listForTask(const QString& workspaceId, const QString& taskId) override;
    [[nodiscard]] shared::common::Result<QVector<application::planner::TaskNoteLink>>
        listForNote(const QString& workspaceId, const QString& noteId) override;

private:
    database::InternalDatabaseManager& m_database;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::infrastructure::planner
