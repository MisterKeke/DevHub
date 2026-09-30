#include "Throttler.h"

#include <algorithm>
#include <utility>

namespace devhub::shared::async {

Throttler::Throttler(int intervalMilliseconds, std::function<void()> callback, QObject* parent)
    : QObject(parent),
      m_intervalMilliseconds(std::max(0, intervalMilliseconds)),
      m_callback(std::move(callback))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_pending) {
            m_pending = false;
            fire();
        }
    });
}

void Throttler::trigger()
{
    if (!m_hasFired || m_intervalMilliseconds == 0
        || m_elapsed.elapsed() >= m_intervalMilliseconds) {
        m_pending = false;
        m_timer.stop();
        fire();
        return;
    }

    m_pending = true;
    const int remaining = m_intervalMilliseconds - static_cast<int>(m_elapsed.elapsed());
    if (!m_timer.isActive()) {
        m_timer.start(std::max(0, remaining));
    }
}

void Throttler::cancelPending()
{
    m_pending = false;
    m_timer.stop();
}

void Throttler::setInterval(int intervalMilliseconds)
{
    m_intervalMilliseconds = std::max(0, intervalMilliseconds);
}

void Throttler::fire()
{
    m_hasFired = true;
    m_elapsed.restart();
    if (m_callback) {
        m_callback();
    }
}

} // namespace devhub::shared::async
