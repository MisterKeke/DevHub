#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

#include <functional>

namespace devhub::shared::async {

class Throttler final : public QObject
{
    Q_OBJECT

public:
    explicit Throttler(int intervalMilliseconds, std::function<void()> callback, QObject* parent = nullptr);
    void trigger();
    void cancelPending();
    void setInterval(int intervalMilliseconds);

private:
    void fire();

    int m_intervalMilliseconds;
    bool m_hasFired = false;
    bool m_pending = false;
    QElapsedTimer m_elapsed;
    QTimer m_timer;
    std::function<void()> m_callback;
};

} // namespace devhub::shared::async
