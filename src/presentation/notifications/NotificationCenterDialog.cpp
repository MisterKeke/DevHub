#include "NotificationCenterDialog.h"
#include "NotificationListModel.h"
#include "../../application/notifications/INotificationRepository.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace devhub::presentation::notifications {
NotificationCenterDialog::NotificationCenterDialog(application::notifications::INotificationRepository& repository,
    QString activeWorkspaceId, QWidget* parent)
    : QDialog(parent), m_repository(repository), m_activeWorkspaceId(std::move(activeWorkspaceId))
{
    setWindowTitle(tr("Notification Center")); resize(700, 520);
    auto* root = new QVBoxLayout(this);
    auto* filters = new QHBoxLayout();
    m_currentWorkspace = new QCheckBox(tr("Current workspace"), this);
    m_currentWorkspace->setEnabled(!m_activeWorkspaceId.isEmpty());
    m_severity = new QComboBox(this); m_severity->addItem(tr("All severity levels"), -1);
    m_severity->addItem(tr("Information"), 0); m_severity->addItem(tr("Success"), 1);
    m_severity->addItem(tr("Warning"), 2); m_severity->addItem(tr("Error"), 3);
    m_category = new QComboBox(this); m_category->addItem(tr("All categories"), -1);
    const QStringList categories{tr("General"), tr("Workspace"), tr("Service"), tr("Port"), tr("Git"), tr("Database"), tr("Security")};
    for (int i = 0; i < categories.size(); ++i) m_category->addItem(categories[i], i);
    m_unreadOnly = new QCheckBox(tr("Unread only"), this);
    filters->addWidget(m_currentWorkspace); filters->addWidget(m_severity); filters->addWidget(m_category); filters->addWidget(m_unreadOnly);
    root->addLayout(filters);
    m_model = new NotificationListModel(repository, this);
    m_list = new QListView(this); m_list->setModel(m_model); m_list->setWordWrap(true); m_list->setUniformItemSizes(false);
    root->addWidget(m_list, 1);
    auto* actions = new QHBoxLayout();
    auto* markRead = new QPushButton(tr("Mark Selected Read"), this);
    auto* clearRead = new QPushButton(tr("Clear Read"), this);
    auto* close = new QPushButton(tr("Close"), this);
    actions->addWidget(markRead); actions->addWidget(clearRead); actions->addStretch(); actions->addWidget(close);
    root->addLayout(actions);
    connect(m_currentWorkspace, &QCheckBox::toggled, this, &NotificationCenterDialog::applyFilters);
    connect(m_unreadOnly, &QCheckBox::toggled, this, &NotificationCenterDialog::applyFilters);
    connect(m_severity, &QComboBox::currentIndexChanged, this, &NotificationCenterDialog::applyFilters);
    connect(m_category, &QComboBox::currentIndexChanged, this, &NotificationCenterDialog::applyFilters);
    connect(markRead, &QPushButton::clicked, this, [this] {
        const auto* item = m_model->notificationAt(m_list->currentIndex().row());
        if (item) (void)m_repository.setRead(item->id, true);
        reload();
    });
    connect(clearRead, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Clear read notifications"), tr("Remove all read notifications from this device?"))
            == QMessageBox::Yes) { (void)m_repository.clearRead(); reload(); }
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    applyFilters();
}
void NotificationCenterDialog::applyFilters()
{
    m_model->setFilters(m_currentWorkspace->isChecked() ? m_activeWorkspaceId : QString{},
        m_severity->currentData().toInt(), m_category->currentData().toInt(), m_unreadOnly->isChecked());
}
void NotificationCenterDialog::reload() { applyFilters(); }
} // namespace devhub::presentation::notifications
