#include "ApplicationEventHub.h"

#include "../tasks/TaskTypes.h"

namespace devhub::application::events {

ApplicationEventHub::ApplicationEventHub(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<notifications::Notification>();
    qRegisterMetaType<lifecycle::ApplicationLifecycleState>();
    qRegisterMetaType<tasks::TaskProgress>();
}

} // namespace devhub::application::events
