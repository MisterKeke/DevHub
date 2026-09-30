#include "ApplicationLifecycle.h"

#include "../events/ApplicationEventHub.h"

namespace devhub::application::lifecycle {
namespace {

bool transitionAllowed(ApplicationLifecycleState current, ApplicationLifecycleState next)
{
    switch (current) {
    case ApplicationLifecycleState::Starting:
        return next == ApplicationLifecycleState::Ready || next == ApplicationLifecycleState::Recovery
            || next == ApplicationLifecycleState::Stopping;
    case ApplicationLifecycleState::Ready:
        return next == ApplicationLifecycleState::Recovery || next == ApplicationLifecycleState::ShutdownRequested;
    case ApplicationLifecycleState::Recovery:
        return next == ApplicationLifecycleState::Starting || next == ApplicationLifecycleState::ShutdownRequested
            || next == ApplicationLifecycleState::Stopping;
    case ApplicationLifecycleState::ShutdownRequested:
        return next == ApplicationLifecycleState::Stopping;
    case ApplicationLifecycleState::Stopping:
        return next == ApplicationLifecycleState::Stopped;
    case ApplicationLifecycleState::Stopped:
        return false;
    }
    return false;
}

} // namespace

ApplicationLifecycle::ApplicationLifecycle(events::ApplicationEventHub& eventHub, QObject* parent)
    : QObject(parent),
      m_eventHub(eventHub)
{
}

bool ApplicationLifecycle::transitionTo(ApplicationLifecycleState nextState)
{
    if (!transitionAllowed(m_state, nextState)) {
        return false;
    }
    m_state = nextState;
    emit m_eventHub.lifecycleStateChanged(m_state);
    return true;
}

} // namespace devhub::application::lifecycle
