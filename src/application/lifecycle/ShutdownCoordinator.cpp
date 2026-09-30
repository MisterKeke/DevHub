#include "ShutdownCoordinator.h"

#include "ApplicationLifecycle.h"
#include "../events/ApplicationEventHub.h"
#include "../tasks/TaskScheduler.h"

#include <QCoreApplication>

namespace devhub::application::lifecycle {

ShutdownCoordinator::ShutdownCoordinator(QCoreApplication& application,
                                         ApplicationLifecycle& lifecycle,
                                         tasks::TaskScheduler& scheduler,
                                         events::ApplicationEventHub& eventHub,
                                         QObject* parent)
    : QObject(parent),
      m_application(application),
      m_lifecycle(lifecycle),
      m_scheduler(scheduler),
      m_eventHub(eventHub)
{
    connect(&m_application, &QCoreApplication::aboutToQuit, this, [this] {
        const auto state = m_lifecycle.state();
        if (state == ApplicationLifecycleState::Ready || state == ApplicationLifecycleState::Recovery) {
            const bool enteredShutdown = m_lifecycle.transitionTo(ApplicationLifecycleState::ShutdownRequested);
            Q_ASSERT(enteredShutdown);
            Q_UNUSED(enteredShutdown);
            emit m_eventHub.shutdownRequested();
            m_scheduler.stopAcceptingAndCancel();
            const bool beganStopping = m_lifecycle.transitionTo(ApplicationLifecycleState::Stopping);
            Q_ASSERT(beganStopping);
            Q_UNUSED(beganStopping);
        } else if (state == ApplicationLifecycleState::Starting) {
            const bool beganStopping = m_lifecycle.transitionTo(ApplicationLifecycleState::Stopping);
            Q_ASSERT(beganStopping);
            Q_UNUSED(beganStopping);
            m_scheduler.stopAcceptingAndCancel();
        }
        if (m_lifecycle.state() == ApplicationLifecycleState::Stopping) {
            const bool stopped = m_lifecycle.transitionTo(ApplicationLifecycleState::Stopped);
            Q_ASSERT(stopped);
            Q_UNUSED(stopped);
        }
    });
}

void ShutdownCoordinator::requestShutdown()
{
    const auto state = m_lifecycle.state();
    if (state == ApplicationLifecycleState::Stopped || state == ApplicationLifecycleState::Stopping
        || state == ApplicationLifecycleState::ShutdownRequested) {
        return;
    }
    if (state == ApplicationLifecycleState::Starting) {
        const bool beganStopping = m_lifecycle.transitionTo(ApplicationLifecycleState::Stopping);
        Q_ASSERT(beganStopping);
        Q_UNUSED(beganStopping);
        m_scheduler.stopAcceptingAndCancel();
        m_application.quit();
        return;
    }
    const bool enteredShutdown = m_lifecycle.transitionTo(ApplicationLifecycleState::ShutdownRequested);
    Q_ASSERT(enteredShutdown);
    Q_UNUSED(enteredShutdown);
    emit m_eventHub.shutdownRequested();
    m_scheduler.stopAcceptingAndCancel();
    const bool beganStopping = m_lifecycle.transitionTo(ApplicationLifecycleState::Stopping);
    Q_ASSERT(beganStopping);
    Q_UNUSED(beganStopping);
    m_application.quit();
}

} // namespace devhub::application::lifecycle
