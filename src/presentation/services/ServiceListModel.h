#pragma once
#include "../../application/services/ServiceTypes.h"
#include <QAbstractListModel>
#include <QHash>

namespace devhub::presentation::services {
class ServiceListModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { ServiceIdRole=Qt::UserRole+1 };
    explicit ServiceListModel(QObject* parent=nullptr):QAbstractListModel(parent){}
    int rowCount(const QModelIndex& parent={}) const override { return parent.isValid()?0:static_cast<int>(m_definitions.size()); }
    QVariant data(const QModelIndex& index,int role=Qt::DisplayRole) const override;
    void setDefinitions(QVector<domain::services::ServiceDefinition> definitions);
    void setRuntimeStates(QVector<application::services::ServiceRuntimeState> states);
    [[nodiscard]] QString serviceId(const QModelIndex& index) const;
private:
    QVector<domain::services::ServiceDefinition> m_definitions;
    QHash<QString,application::services::ServiceRuntimeState> m_states;
};
}
