#pragma once
#include "../../application/notifications/Notification.h"
#include <QObject>
#include <QQueue>
#include <QHash>
#include <QDateTime>

class QLabel;
class QTimer;
class QWidget;
namespace devhub::presentation::notifications {
class ToastManager final : public QObject {
    Q_OBJECT
public:
    explicit ToastManager(QWidget& owner, QObject* parent = nullptr);
public slots:
    void enqueue(application::notifications::Notification notification);
private slots:
    void showNext();
private:
    QWidget& m_owner;
    QLabel* m_toast = nullptr;
    QTimer* m_timer = nullptr;
    QQueue<application::notifications::Notification> m_queue;
    QHash<QString, QDateTime> m_lastShown;
};
} // namespace devhub::presentation::notifications
