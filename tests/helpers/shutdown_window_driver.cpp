#include "application/events/ApplicationEventHub.h"
#include "application/lifecycle/ApplicationLifecycle.h"
#include "application/lifecycle/ShutdownCoordinator.h"
#include "application/tasks/TaskScheduler.h"
#include "shared/identifiers/IdGenerator.h"

#include <QApplication>
#include <QCloseEvent>
#include <QTimer>
#include <QWidget>
#include <cstdio>

namespace
{
// Match the shell's deferred close: transition guards run once, then close
// events are rejected while the coordinator drains asynchronous participants.
class DeferredCloseWindow final : public QWidget
{
  public:
    explicit DeferredCloseWindow(devhub::application::lifecycle::ShutdownCoordinator &shutdown)
        : m_shutdown(shutdown)
    {
    }
    bool cancelNextClose = false;
    int shutdownRequests = 0;

  protected:
    void closeEvent(QCloseEvent *event) override
    {
        event->ignore();
        if (cancelNextClose)
        {
            cancelNextClose = false;
            return;
        }
        if (shutdownRequests != 0)
            return;
        ++shutdownRequests;
        m_shutdown.requestShutdown();
    }

  private:
    devhub::application::lifecycle::ShutdownCoordinator &m_shutdown;
};
} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    using namespace devhub::application;
    const QString mode = application.arguments().value(1);
    events::ApplicationEventHub events;
    lifecycle::ApplicationLifecycle lifecycle(events);
    if (!lifecycle.transitionTo(lifecycle::ApplicationLifecycleState::Ready))
        return 1;
    devhub::shared::identifiers::UuidIdGenerator ids;
    tasks::TaskScheduler scheduler(ids, 1);
    lifecycle::ShutdownCoordinator shutdown(application, lifecycle, scheduler, events, nullptr,
                                            120);
    DeferredCloseWindow window(shutdown);
    QObject participant;
    int participantStarts = 0;
    bool participantCompleted = false;
    if (mode != QStringLiteral("empty") && mode != QStringLiteral("cancel"))
    {
        shutdown.addPreQuitParticipant(
            &participant,
            [&](auto done)
            {
                ++participantStarts;
                if (mode == QStringLiteral("deadline"))
                    return;
                const auto complete = [&, done]
                {
                    participantCompleted = true;
                    done();
                    done(); // Duplicate completions must not finalize shutdown twice.
                };
                if (mode == QStringLiteral("delayed"))
                    QTimer::singleShot(40, &participant, complete);
                else
                    complete();
            });
    }
    int finishedCount = 0;
    int quitCount = 0;
    bool prematurelyFinished = false;
    QObject::connect(&shutdown, &lifecycle::ShutdownCoordinator::shutdownFinished, &application,
                     [&]
                     {
                         ++finishedCount;
                         prematurelyFinished =
                             mode == QStringLiteral("delayed") && !participantCompleted;
                     });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application,
                     [&] { ++quitCount; });
    window.show();
    QTimer::singleShot(0, &window,
                       [&]
                       {
                           if (mode == QStringLiteral("cancel"))
                           {
                               window.cancelNextClose = true;
                               window.close();
                               if (!scheduler.isAccepting() || window.shutdownRequests != 0 ||
                                   lifecycle.state() != lifecycle::ApplicationLifecycleState::Ready)
                               {
                                   application.exit(2);
                                   return;
                               }
                           }
                           window.close();
                       });
    if (mode == QStringLiteral("delayed") || mode == QStringLiteral("deadline"))
        QTimer::singleShot(10, &window, [&] { window.close(); });
    QTimer::singleShot(1500, &application, [&] { application.exit(90); });
    const int result = application.exec();
    std::printf("exit=%d shutdown=%d aboutToQuit=%d requests=%d participants=%d\n", result,
                finishedCount, quitCount, window.shutdownRequests, participantStarts);
    if (result != 0)
        return result;
    if (finishedCount != 1 || quitCount != 1 || window.shutdownRequests != 1 ||
        prematurelyFinished || scheduler.isAccepting() ||
        lifecycle.state() != lifecycle::ApplicationLifecycleState::Stopped)
        return 3;
    return 0;
}
