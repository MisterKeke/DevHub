#pragma once

#include "../lifecycle/ApplicationLifecycleState.h"
#include "../notifications/Notification.h"
#include "../tasks/TaskTypes.h"

#include <QObject>
#include <QUuid>

namespace devhub::application::events {

class ApplicationEventHub final : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationEventHub(QObject* parent = nullptr);

signals:
    void lifecycleStateChanged(lifecycle::ApplicationLifecycleState state);
    void notificationRaised(notifications::Notification notification);
    void workspaceStateChanged(QUuid workspaceId, quint64 generation);
    void taskProgressChanged(tasks::TaskProgress progress);
    void shutdownRequested();
};

} // namespace devhub::application::events
