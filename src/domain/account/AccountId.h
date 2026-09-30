#pragma once

#include "../../shared/common/Result.h"
#include "../../shared/identifiers/IdGenerator.h"

#include <QUuid>
#include <QString>

#include <utility>

namespace devhub::domain::account {

class AccountId final
{
public:
    [[nodiscard]] static shared::common::Result<AccountId> create(shared::identifiers::IIdGenerator& idGenerator);
    [[nodiscard]] static shared::common::Result<AccountId> fromString(const QString& value);
    [[nodiscard]] const QUuid& value() const noexcept { return m_value; }
    [[nodiscard]] QString toString() const { return m_value.toString(QUuid::WithoutBraces); }

    friend bool operator==(const AccountId&, const AccountId&) = default;

private:
    explicit AccountId(QUuid value);
    QUuid m_value;
};

} // namespace devhub::domain::account
