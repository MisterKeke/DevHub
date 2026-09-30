#pragma once

#include "PortTableModel.h"
#include "../../application/ports/PortMonitorService.h"

#include <QWidget>

class QLineEdit;
class QLabel;
class QComboBox;
class QTableView;
class QTimer;

namespace devhub::application::services { class ServiceCoordinator; }
namespace devhub::application::workspaces { class WorkspaceManager; }

namespace devhub::presentation::ports {

class PortPage final : public QWidget
{
    Q_OBJECT
public:
    PortPage(application::ports::PortMonitorService& monitor,
             application::services::ServiceCoordinator& services,
             application::workspaces::WorkspaceManager& workspaces,
             QWidget* parent = nullptr);

signals:
    void openWorkspaceRequested(QString workspaceId);

private:
    [[nodiscard]] const application::ports::PortRecord* selectedRecord() const;
    void copySelected(bool copyPort);
    void refreshAssociations(QVector<application::ports::PortRecord>& records);

    application::ports::PortMonitorService& m_monitor;
    application::services::ServiceCoordinator& m_services;
    application::workspaces::WorkspaceManager& m_workspaces;
    PortTableModel* m_model = nullptr;
    PortFilterProxyModel* m_proxy = nullptr;
    QTableView* m_table = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_endpointFilter = nullptr;
    QLabel* m_status = nullptr;
    QTimer* m_refreshTimer = nullptr;
};

} // namespace devhub::presentation::ports
