#pragma once

#include "AccountId.h"
#include "Username.h"

#include <QDateTime>

#include <optional>

namespace devhub::infrastructure::account {
class SqlAccountRepository;
}

namespace devhub::domain::account {

class Account final
{
public:
    [[nodiscard]] const AccountId& id() const noexcept { return m_id; }
    [[nodiscard]] const DisplayUsername& displayUsername() const noexcept { return m_displayUsername; }
    [[nodiscard]] const NormalizedUsername& normalizedUsername() const noexcept { return m_normalizedUsername; }
    [[nodiscard]] const QDateTime& createdAtUtc() const noexcept { return m_createdAtUtc; }
    [[nodiscard]] const QDateTime& updatedAtUtc() const noexcept { return m_updatedAtUtc; }
    [[nodiscard]] const std::optional<QDateTime>& lastLoginAtUtc() const noexcept { return m_lastLoginAtUtc; }

private:
    Account(AccountId id,
            DisplayUsername displayUsername,
            NormalizedUsername normalizedUsername,
            QDateTime createdAtUtc,
            QDateTime updatedAtUtc,
            std::optional<QDateTime> lastLoginAtUtc);

    AccountId m_id;
    DisplayUsername m_displayUsername;
    NormalizedUsername m_normalizedUsername;
    QDateTime m_createdAtUtc;
    QDateTime m_updatedAtUtc;
    std::optional<QDateTime> m_lastLoginAtUtc;

    friend class devhub::infrastructure::account::SqlAccountRepository;
};

} // namespace devhub::domain::account
