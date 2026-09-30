#pragma once
#include "../../application/notifications/INotificationRepository.h"
#include <QAbstractListModel>

namespace devhub::presentation::notifications {
class NotificationListModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1, MessageRole, SeverityRole, CategoryRole, TimestampRole,
                 WorkspaceRole, ReadRole, RepeatCountRole };
    explicit NotificationListModel(application::notifications::INotificationRepository& repository, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    void setFilters(QString workspaceId, int severity, int category, bool unreadOnly);
    [[nodiscard]] bool reload();
    [[nodiscard]] const application::notifications::Notification* notificationAt(int row) const;
    [[nodiscard]] QString lastError() const { return m_lastError; }
private:
    application::notifications::INotificationRepository& m_repository;
    QVector<application::notifications::Notification> m_items;
    QString m_workspaceId;
    QString m_lastError;
    int m_severity = -1;
    int m_category = -1;
    bool m_unreadOnly = false;
};
} // namespace devhub::presentation::notifications
