#include "NotificationListModel.h"
#include <QLocale>
#include <QFont>
#include <QSize>

namespace devhub::presentation::notifications {
using application::notifications::Notification;
NotificationListModel::NotificationListModel(application::notifications::INotificationRepository& repository, QObject* parent)
    : QAbstractListModel(parent), m_repository(repository) { (void)reload(); }
int NotificationListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_items.size()); }
QVariant NotificationListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) return {};
    const auto& item = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole: return QStringLiteral("%1%2\n%3").arg(item.isRead ? QString{} : QStringLiteral("● ")).arg(item.title,
        item.repeatCount > 1 ? QStringLiteral(" ×%1").arg(item.repeatCount) : QString{}, item.message);
    case Qt::FontRole: { QFont font; font.setBold(!item.isRead); return font; }
    case Qt::SizeHintRole: return QSize(420, 60);
    case Qt::ToolTipRole: return QLocale().toString(item.createdAtUtc.toLocalTime(), QLocale::ShortFormat);
    case IdRole: return item.id.toString(QUuid::WithoutBraces);
    case MessageRole: return item.message;
    case SeverityRole: return static_cast<int>(item.severity);
    case CategoryRole: return static_cast<int>(item.category);
    case TimestampRole: return item.createdAtUtc;
    case WorkspaceRole: return item.workspaceId ? item.workspaceId->toString(QUuid::WithoutBraces) : QString{};
    case ReadRole: return item.isRead;
    case RepeatCountRole: return item.repeatCount;
    default: return {};
    }
}
void NotificationListModel::setFilters(QString workspaceId, int severity, int category, bool unreadOnly)
{
    m_workspaceId = std::move(workspaceId); m_severity = severity; m_category = category; m_unreadOnly = unreadOnly; (void)reload();
}
bool NotificationListModel::reload()
{
    const auto result = m_repository.list(m_workspaceId, m_severity, m_category, m_unreadOnly);
    if (!result) { m_lastError = result.error().userMessage; return false; }
    beginResetModel(); m_items = result.value(); m_lastError.clear(); endResetModel(); return true;
}
const Notification* NotificationListModel::notificationAt(int row) const
{
    return row >= 0 && row < m_items.size() ? &m_items.at(row) : nullptr;
}
} // namespace devhub::presentation::notifications
