#include "InactivityMonitor.h"

#include "SessionController.h"
#include "SessionState.h"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>

namespace devhub::application::authentication {

InactivityMonitor::InactivityMonitor(QCoreApplication& application,
                                     SessionController& session,
                                     IMonotonicClock& clock,
                                     int inactivityTimeoutMs,
                                     QObject* parent)
    : QObject(parent),
      m_application(application),
      m_session(session),
      m_clock(clock),
      m_timeoutMs(std::max(1000, inactivityTimeoutMs))
{
    m_checkTimer.setInterval(1000);
    connect(&m_checkTimer, &QTimer::timeout, this, &InactivityMonitor::checkTimeout);
    connect(&m_session, &SessionController::stateChanged, this, [this] { refreshMonitoring(); });
    connect(&m_session, &SessionController::keepSessionChanged, this, [this] { refreshMonitoring(); });
    m_application.installEventFilter(this);
    refreshMonitoring();
}

InactivityMonitor::~InactivityMonitor()
{
    m_application.removeEventFilter(this);
}

bool InactivityMonitor::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (m_session.state() == SessionState::SignedIn && !m_session.keepSession()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TabletPress:
            resetActivity();
            break;
        default:
            break;
        }
    }
    return false;
}

void InactivityMonitor::refreshMonitoring()
{
    if (m_session.state() == SessionState::SignedIn && !m_session.keepSession()) {
        resetActivity();
        m_checkTimer.start();
        return;
    }
    m_checkTimer.stop();
}

void InactivityMonitor::resetActivity()
{
    m_clock.restart();
}

void InactivityMonitor::checkTimeout()
{
    if (m_session.state() != SessionState::SignedIn || m_session.keepSession()
        || m_clock.elapsedMs() < m_timeoutMs) {
        return;
    }
    m_session.beginLock();
}

} // namespace devhub::application::authentication
