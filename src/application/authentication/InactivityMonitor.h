#pragma once

#include <QObject>
#include <QTimer>

#include "IMonotonicClock.h"

class QCoreApplication;

namespace devhub::application::authentication {

class SessionController;

class InactivityMonitor final : public QObject
{
    Q_OBJECT
public:
    InactivityMonitor(QCoreApplication& application,
                      SessionController& session,
                      IMonotonicClock& clock,
                      int inactivityTimeoutMs,
                      QObject* parent = nullptr);
    ~InactivityMonitor() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
    void checkTimeout();

private:
    void refreshMonitoring();
    void resetActivity();

    QCoreApplication& m_application;
    SessionController& m_session;
    IMonotonicClock& m_clock;
    int m_timeoutMs;
    QTimer m_checkTimer;
};

} // namespace devhub::application::authentication
