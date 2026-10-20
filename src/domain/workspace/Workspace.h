#pragma once

#include "WorkspaceId.h"
#include "WorkspacePath.h"
#include "WorkspaceStatus.h"
#include "../account/AccountId.h"

#include <QDateTime>
#include <QString>
#include <optional>
#include <utility>

namespace devhub::domain::workspace {

class Workspace final
{
public:
    Workspace(WorkspaceId id, account::AccountId accountId, QString displayName, WorkspacePath path,
              WorkspaceStatus status, bool favorite, int recentOrder,
              QDateTime createdAtUtc, std::optional<QDateTime> lastOpenedAtUtc = std::nullopt,
              std::optional<QDateTime> archivedAtUtc = std::nullopt)
        : m_id(std::move(id)), m_accountId(std::move(accountId)), m_displayName(std::move(displayName)),
          m_path(std::move(path)), m_status(status), m_favorite(favorite), m_recentOrder(recentOrder),
          m_createdAtUtc(std::move(createdAtUtc)), m_lastOpenedAtUtc(std::move(lastOpenedAtUtc)),
          m_archivedAtUtc(std::move(archivedAtUtc)) {}
    [[nodiscard]] const WorkspaceId& id() const noexcept { return m_id; }
    [[nodiscard]] const account::AccountId& accountId() const noexcept { return m_accountId; }
    [[nodiscard]] const QString& displayName() const noexcept { return m_displayName; }
    [[nodiscard]] const WorkspacePath& path() const noexcept { return m_path; }
    [[nodiscard]] WorkspaceStatus status() const noexcept { return m_status; }
    [[nodiscard]] bool isFavorite() const noexcept { return m_favorite; }
    [[nodiscard]] int recentOrder() const noexcept { return m_recentOrder; }
    [[nodiscard]] const QDateTime& createdAtUtc() const noexcept { return m_createdAtUtc; }
    [[nodiscard]] const std::optional<QDateTime>& lastOpenedAtUtc() const noexcept { return m_lastOpenedAtUtc; }
    [[nodiscard]] const std::optional<QDateTime>& archivedAtUtc() const noexcept { return m_archivedAtUtc; }
    [[nodiscard]] bool isArchived() const noexcept { return m_archivedAtUtc.has_value(); }

private:
    WorkspaceId m_id;
    account::AccountId m_accountId;
    QString m_displayName;
    WorkspacePath m_path;
    WorkspaceStatus m_status;
    bool m_favorite;
    int m_recentOrder;
    QDateTime m_createdAtUtc;
    std::optional<QDateTime> m_lastOpenedAtUtc;
    std::optional<QDateTime> m_archivedAtUtc;
};

} // namespace devhub::domain::workspace
