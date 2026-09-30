#pragma once

#include "Notification.h"
#include "../../application/events/ApplicationEventHub.h"
#include "../../shared/identifiers/IdGenerator.h"
#include "../../shared/time/ApplicationClock.h"

#include <QHash>
#include <QObject>

#include <chrono>

namespace devhub::application::notifications {

class NotificationService final : public QObject
{
    Q_OBJECT

public:
    NotificationService(const shared::time::IApplicationClock& clock,
                        shared::identifiers::IIdGenerator& idGenerator,
                        events::ApplicationEventHub& eventHub,
                        QObject* parent = nullptr);

    void setMinimumRepeatInterval(std::chrono::milliseconds interval);
    [[nodiscard]] bool post(NotificationDraft notification);

private:
    const shared::time::IApplicationClock& m_clock;
    shared::identifiers::IIdGenerator& m_idGenerator;
    events::ApplicationEventHub& m_eventHub;
    std::chrono::milliseconds m_minimumRepeatInterval{3000};
    QHash<QString, QDateTime> m_lastPostedUtcByKey;
};

} // namespace devhub::application::notifications
