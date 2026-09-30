#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

namespace devhub::shared::async {

class Debouncer final : public QObject
{
    Q_OBJECT

public:
    explicit Debouncer(int delayMilliseconds, std::function<void()> callback, QObject* parent = nullptr);
    void trigger();
    void cancel();
    void setDelay(int delayMilliseconds);

private:
    QTimer m_timer;
    std::function<void()> m_callback;
};

} // namespace devhub::shared::async
