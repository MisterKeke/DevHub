#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <QList>

class QCoreApplication;

namespace devhub::application::events {
class ApplicationEventHub;
}
namespace devhub::application::lifecycle {
class ApplicationLifecycle;
}
namespace devhub::application::tasks {
class TaskScheduler;
}

namespace devhub::application::lifecycle {

class ShutdownCoordinator final : public QObject
{
    Q_OBJECT

public:
    using Completion = std::function<void()>;
    using PreQuitStart = std::function<void(Completion)>;
    ShutdownCoordinator(QCoreApplication& application,
                        ApplicationLifecycle& lifecycle,
                        tasks::TaskScheduler& scheduler,
                        events::ApplicationEventHub& eventHub,
                        QObject* parent = nullptr,
                        int participantDeadlineMilliseconds = 10000);
    void requestShutdown();
    void addPreQuitParticipant(QObject* owner, PreQuitStart start);

signals:
    void shutdownFinished();

private:
    struct Participant final {
        QPointer<QObject> owner;
        PreQuitStart start;
        bool completed = false;
    };
    void completeParticipant(qsizetype index);
    void finishShutdown();
    void emergencyShutdown();

    QCoreApplication& m_application;
    ApplicationLifecycle& m_lifecycle;
    tasks::TaskScheduler& m_scheduler;
    events::ApplicationEventHub& m_eventHub;
    QList<Participant> m_participants;
    QTimer m_deadline;
    bool m_shutdownStarted = false;
    bool m_finished = false;
    int m_participantDeadlineMilliseconds = 10000;
};

} // namespace devhub::application::lifecycle
