#pragma once
#include "../../application/services/IServiceDefinitionRepository.h"
#include "../../application/services/ServiceCoordinator.h"
#include "ServiceListModel.h"
#include <QWidget>

class QListView; class QPlainTextEdit; class QLabel;
namespace devhub::presentation::services {
class ServicePage final : public QWidget {
    Q_OBJECT
public:
    ServicePage(application::services::IServiceDefinitionRepository& repository,
                application::services::ServiceCoordinator& coordinator, QWidget* parent = nullptr);
    void setWorkspace(QString id, QString root);
private:
    void reload();
    void editDefinition(const std::optional<domain::services::ServiceDefinition>& existing = std::nullopt);
    application::services::IServiceDefinitionRepository& m_repository;
    application::services::ServiceCoordinator& m_coordinator;
    QString m_workspaceId;
    QString m_root;
    QListView* m_list = nullptr;
    ServiceListModel* m_model = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QLabel* m_status = nullptr;
};
}
