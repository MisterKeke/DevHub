#include "ShutdownCoordinator.h"

#include "ApplicationLifecycle.h"
#include "../events/ApplicationEventHub.h"
#include "../tasks/TaskScheduler.h"

#include <QCoreApplication>
#include <QMetaObject>

namespace devhub::application::lifecycle {

ShutdownCoordinator::ShutdownCoordinator(QCoreApplication& application,
                                         ApplicationLifecycle& lifecycle,
                                         tasks::TaskScheduler& scheduler,
                                         events::ApplicationEventHub& eventHub,
                                         QObject* parent,
                                         int participantDeadlineMilliseconds)
    : QObject(parent),
      m_application(application),
      m_lifecycle(lifecycle),
      m_scheduler(scheduler),
      m_eventHub(eventHub),
      m_participantDeadlineMilliseconds(qMax(1, participantDeadlineMilliseconds))
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, &ShutdownCoordinator::finishShutdown);
    connect(&m_application, &QCoreApplication::aboutToQuit,
            this, &ShutdownCoordinator::emergencyShutdown);
}

void ShutdownCoordinator::addPreQuitParticipant(QObject* owner, PreQuitStart start)
{
    if (m_shutdownStarted || m_finished || owner == nullptr || !start) return;
    const qsizetype index = m_participants.size();
    m_participants.append({owner, std::move(start), false});
    connect(owner, &QObject::destroyed, this, [this, index] {
        if (m_shutdownStarted) completeParticipant(index);
    });
}

void ShutdownCoordinator::requestShutdown()
{
    if (m_shutdownStarted || m_finished) return;
    const auto state = m_lifecycle.state();
    if (state == ApplicationLifecycleState::Stopped || state == ApplicationLifecycleState::Stopping
        || state == ApplicationLifecycleState::ShutdownRequested) {
        finishShutdown();
        return;
    }
    m_shutdownStarted = true;
    if (state == ApplicationLifecycleState::Starting) {
        const bool beganStopping = m_lifecycle.transitionTo(ApplicationLifecycleState::Stopping);
        Q_ASSERT(beganStopping);
        Q_UNUSED(beganStopping);
        m_scheduler.stopAcceptingAndCancel();
        finishShutdown();
        return;
    }

    const bool enteredShutdown = m_lifecycle.transitionTo(ApplicationLifecycleState::ShutdownRequested);
    Q_ASSERT(enteredShutdown);
    Q_UNUSED(enteredShutdown);
    emit m_eventHub.shutdownRequested();
    m_scheduler.stopAcceptingAndCancel();

    // Keep a bounded event-loop window for participants to interrupt work and
    // observe child exit. The aboutToQuit hook below is only a last-resort path.
    m_deadline.start(m_participantDeadlineMilliseconds);
    if (m_participants.isEmpty()) {
        finishShutdown();
        return;
    }
    for (qsizetype index = 0; index < m_participants.size(); ++index) {
        auto& participant = m_participants[index];
        if (!participant.owner) {
            completeParticipant(index);
            continue;
        }
        QPointer<ShutdownCoordinator> self(this);
        participant.start([self, index] {
            if (!self) return;
            QMetaObject::invokeMethod(self, [self, index] {
                if (self) self->completeParticipant(index);
            }, Qt::QueuedConnection);
        });
    }
}

void ShutdownCoordinator::completeParticipant(qsizetype index)
{
    if (!m_shutdownStarted || m_finished || index < 0 || index >= m_participants.size()) return;
    auto& participant = m_participants[index];
    if (participant.completed) return;
    participant.completed = true;
    for (const auto& item : m_participants) if (!item.completed) return;
    finishShutdown();
}

void ShutdownCoordinator::finishShutdown()
{
    if (m_finished) return;
    m_finished = true;
    m_deadline.stop();
    if (m_lifecycle.state() == ApplicationLifecycleState::ShutdownRequested) {
        const bool beganStopping = m_lifecycle.transitionTo(ApplicationLifecycleState::Stopping);
        Q_ASSERT(beganStopping);
        Q_UNUSED(beganStopping);
    }
    if (m_lifecycle.state() == ApplicationLifecycleState::Stopping) {
        const bool stopped = m_lifecycle.transitionTo(ApplicationLifecycleState::Stopped);
        Q_ASSERT(stopped);
        Q_UNUSED(stopped);
    }
    emit shutdownFinished();
    // The shell has already accepted its save/service guards and deliberately
    // rejects further close events while participants drain. QApplication::quit
    // sends another close request which that guard can veto, leaving an already
    // finalized application running. Exit the event loop directly at this point;
    // aboutToQuit and normal owner destruction still perform their cleanup.
    m_application.exit(0);
}

void ShutdownCoordinator::emergencyShutdown()
{
    if (m_finished) return;
    // aboutToQuit is emitted after the event loop has begun exiting. Do not wait
    // for timers or asynchronous participants here; owners have their own bounded
    // destructor fallback for this exceptional path.
    m_deadline.stop();
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
    } else if (state == ApplicationLifecycleState::ShutdownRequested) {
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
    m_finished = true;
}

} // namespace devhub::application::lifecycle
