#include "ToastManager.h"
#include "../theme/ThemePalette.h"

#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace devhub::presentation::notifications {
ToastManager::ToastManager(QWidget& owner, QObject* parent) : QObject(parent), m_owner(owner)
{
    m_toast = new QLabel(&owner, Qt::ToolTip);
    m_toast->setWordWrap(true); m_toast->setMinimumWidth(280); m_toast->setMaximumWidth(420);
    m_toast->setStyleSheet(QStringLiteral("QLabel { background:%1; color:%2; border:1px solid %3; border-radius: 0px; padding:14px; }")
        .arg(devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::RaisedSurface).name(),
             devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::TextPrimary).name(),
             devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::BorderStrong).name()));
    m_timer = new QTimer(this); m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ToastManager::showNext);
}
void ToastManager::enqueue(application::notifications::Notification notification)
{
    const QString key = notification.deduplicationKey;
    const auto last = m_lastShown.constFind(key);
    if (!key.isEmpty() && last != m_lastShown.cend() && last->msecsTo(QDateTime::currentDateTimeUtc()) < 3000) return;
    if (!key.isEmpty()) m_lastShown.insert(key, QDateTime::currentDateTimeUtc());
    if (m_queue.size() >= 20) m_queue.dequeue();
    m_queue.enqueue(std::move(notification));
    if (!m_timer->isActive()) showNext();
}
void ToastManager::showNext()
{
    if (m_queue.isEmpty() || !m_owner.isVisible()) { m_toast->hide(); return; }
    const auto notification = m_queue.dequeue();
    m_toast->setText(QStringLiteral("%1\n%2").arg(notification.title, notification.message));
    m_toast->adjustSize();
    const QPoint bottomRight = m_owner.mapToGlobal(QPoint(m_owner.width() - 24, m_owner.height() - 24));
    m_toast->move(bottomRight.x() - m_toast->width(), bottomRight.y() - m_toast->height());
    m_toast->show(); m_toast->raise(); m_timer->start(5000);
}
} // namespace devhub::presentation::notifications
