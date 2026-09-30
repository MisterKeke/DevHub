#pragma once

#include "../../shared/common/Result.h"

#include <QString>

namespace devhub::domain::account {

inline constexpr int MaximumUsernameCodePoints = 64;

class NormalizedUsername;

class DisplayUsername final
{
public:
    [[nodiscard]] static shared::common::Result<DisplayUsername> create(QString value);
    [[nodiscard]] const QString& value() const noexcept { return m_value; }

private:
    explicit DisplayUsername(QString value);
    QString m_value;
};

class NormalizedUsername final
{
public:
    [[nodiscard]] static shared::common::Result<NormalizedUsername> fromDisplay(const DisplayUsername& value);
    [[nodiscard]] static shared::common::Result<NormalizedUsername> create(QString value);
    [[nodiscard]] const QString& value() const noexcept { return m_value; }

    friend bool operator==(const NormalizedUsername&, const NormalizedUsername&) = default;

private:
    explicit NormalizedUsername(QString value);
    QString m_value;
};

} // namespace devhub::domain::account
