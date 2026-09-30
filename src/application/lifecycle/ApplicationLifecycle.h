#pragma once

#include "ApplicationLifecycleState.h"

#include <QObject>

namespace devhub::application::events {
class ApplicationEventHub;
}

namespace devhub::application::lifecycle {

class ApplicationLifecycle final : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationLifecycle(events::ApplicationEventHub& eventHub, QObject* parent = nullptr);
    [[nodiscard]] ApplicationLifecycleState state() const noexcept { return m_state; }
    [[nodiscard]] bool transitionTo(ApplicationLifecycleState nextState);

private:
    events::ApplicationEventHub& m_eventHub;
    ApplicationLifecycleState m_state = ApplicationLifecycleState::Starting;
};

} // namespace devhub::application::lifecycle
