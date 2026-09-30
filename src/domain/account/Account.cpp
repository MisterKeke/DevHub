#include "Account.h"

#include <utility>

namespace devhub::domain::account {

Account::Account(AccountId id,
                 DisplayUsername displayUsername,
                 NormalizedUsername normalizedUsername,
                 QDateTime createdAtUtc,
                 QDateTime updatedAtUtc,
                 std::optional<QDateTime> lastLoginAtUtc)
    : m_id(std::move(id)),
      m_displayUsername(std::move(displayUsername)),
      m_normalizedUsername(std::move(normalizedUsername)),
      m_createdAtUtc(std::move(createdAtUtc)),
      m_updatedAtUtc(std::move(updatedAtUtc)),
      m_lastLoginAtUtc(std::move(lastLoginAtUtc))
{
}

} // namespace devhub::domain::account
