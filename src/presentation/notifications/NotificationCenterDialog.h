#pragma once
#include <QDialog>
#include <QString>

class QComboBox;
class QListView;
class QCheckBox;
namespace devhub::presentation::notifications { class NotificationListModel; }
namespace devhub::application::notifications { class INotificationRepository; }

namespace devhub::presentation::notifications {
class NotificationCenterDialog final : public QDialog {
    Q_OBJECT
public:
    NotificationCenterDialog(application::notifications::INotificationRepository& repository,
                             QString activeWorkspaceId = {}, QWidget* parent = nullptr);
    void reload();
private:
    void applyFilters();
    application::notifications::INotificationRepository& m_repository;
    NotificationListModel* m_model = nullptr;
    QListView* m_list = nullptr;
    QComboBox* m_severity = nullptr;
    QComboBox* m_category = nullptr;
    QCheckBox* m_currentWorkspace = nullptr;
    QCheckBox* m_unreadOnly = nullptr;
    QString m_activeWorkspaceId;
};
} // namespace devhub::presentation::notifications
