#pragma once

#include "IMonotonicClock.h"

#include <QElapsedTimer>

namespace devhub::application::authentication {

class SystemMonotonicClock final : public IMonotonicClock
{
public:
    void restart() override { m_timer.start(); }
    [[nodiscard]] qint64 elapsedMs() const override
    {
        return m_timer.isValid() ? m_timer.elapsed() : 0;
    }

private:
    QElapsedTimer m_timer;
};

} // namespace devhub::application::authentication
