#include "NotificationService.h"

#include <QThread>

#include <algorithm>
#include <utility>

namespace devhub::application::notifications {

NotificationService::NotificationService(const shared::time::IApplicationClock& clock,
                                         shared::identifiers::IIdGenerator& idGenerator,
                                         events::ApplicationEventHub& eventHub,
                                         QObject* parent)
    : QObject(parent),
      m_clock(clock),
      m_idGenerator(idGenerator),
      m_eventHub(eventHub)
{
}

void NotificationService::setMinimumRepeatInterval(std::chrono::milliseconds interval)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_minimumRepeatInterval = std::max(interval, std::chrono::milliseconds::zero());
}

bool NotificationService::post(NotificationDraft draft)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QString key = draft.deduplicationKey.isEmpty()
        ? QStringLiteral("%1|%2|%3").arg(static_cast<int>(draft.category)).arg(draft.title, draft.message)
        : draft.deduplicationKey;
    const QDateTime now = m_clock.utcNow();
    const auto previous = m_lastPostedUtcByKey.constFind(key);
    if (previous != m_lastPostedUtcByKey.cend()
        && previous->msecsTo(now) < m_minimumRepeatInterval.count()) {
        return false;
    }

    Notification notification;
    notification.id = m_idGenerator.create();
    notification.severity = draft.severity;
    notification.category = draft.category;
    notification.title = std::move(draft.title);
    notification.message = std::move(draft.message);
    notification.deduplicationKey = key;
    notification.createdAtUtc = now;
    notification.workspaceId = draft.workspaceId;
    notification.correlationId = draft.correlationId;
    m_lastPostedUtcByKey.insert(key, now);

    if (m_lastPostedUtcByKey.size() > 512) {
        const QDateTime expiration = now.addSecs(-60);
        for (auto it = m_lastPostedUtcByKey.begin(); it != m_lastPostedUtcByKey.end();) {
            if (it.value() < expiration) {
                it = m_lastPostedUtcByKey.erase(it);
            } else {
                ++it;
            }
        }
    }

    emit m_eventHub.notificationRaised(std::move(notification));
    return true;
}

} // namespace devhub::application::notifications
