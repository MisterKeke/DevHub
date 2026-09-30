#pragma once

#include <QObject>

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
    ShutdownCoordinator(QCoreApplication& application,
                        ApplicationLifecycle& lifecycle,
                        tasks::TaskScheduler& scheduler,
                        events::ApplicationEventHub& eventHub,
                        QObject* parent = nullptr);
    void requestShutdown();

private:
    QCoreApplication& m_application;
    ApplicationLifecycle& m_lifecycle;
    tasks::TaskScheduler& m_scheduler;
    events::ApplicationEventHub& m_eventHub;
};

} // namespace devhub::application::lifecycle
