#pragma once

#include <QtGlobal>

namespace devhub::application::authentication {

class IMonotonicClock
{
public:
    virtual ~IMonotonicClock() = default;
    virtual void restart() = 0;
    [[nodiscard]] virtual qint64 elapsedMs() const = 0;
};

} // namespace devhub::application::authentication
