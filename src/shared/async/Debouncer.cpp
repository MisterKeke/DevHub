#include "Debouncer.h"

#include <algorithm>
#include <utility>

namespace devhub::shared::async {

Debouncer::Debouncer(int delayMilliseconds, std::function<void()> callback, QObject* parent)
    : QObject(parent),
      m_callback(std::move(callback))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(std::max(0, delayMilliseconds));
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_callback) {
            m_callback();
        }
    });
}

void Debouncer::trigger()
{
    m_timer.start();
}

void Debouncer::cancel()
{
    m_timer.stop();
}

void Debouncer::setDelay(int delayMilliseconds)
{
    m_timer.setInterval(std::max(0, delayMilliseconds));
}

} // namespace devhub::shared::async
