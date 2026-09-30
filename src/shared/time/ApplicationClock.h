#pragma once

#include <QDateTime>

namespace devhub::shared::time {

class IApplicationClock
{
public:
    virtual ~IApplicationClock() = default;
    [[nodiscard]] virtual QDateTime utcNow() const = 0;
};

class SystemApplicationClock final : public IApplicationClock
{
public:
    [[nodiscard]] QDateTime utcNow() const override;
};

} // namespace devhub::shared::time
