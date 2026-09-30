#include "ServiceListModel.h"

namespace devhub::presentation::services {
QVariant ServiceListModel::data(const QModelIndex& index,int role) const {
    if(!index.isValid()||index.row()<0||index.row()>=m_definitions.size())return {};
    const auto& definition=m_definitions.at(index.row());
    if(role==ServiceIdRole)return definition.id;
    if(role!=Qt::DisplayRole)return {};
    QString state=tr("Stopped");const auto runtime=m_states.constFind(definition.id);
    if(runtime!=m_states.cend())switch(runtime->lifecycle){
    case application::services::ServiceLifecycleState::Starting:state=tr("Starting");break;
    case application::services::ServiceLifecycleState::Running:state=runtime->health==application::services::ServiceHealthState::Healthy?tr("Healthy"):tr("Running");break;
    case application::services::ServiceLifecycleState::Stopping:state=tr("Stopping");break;
    case application::services::ServiceLifecycleState::Failed:state=tr("Failed: %1").arg(runtime->error);break;default:break;}
    return QStringLiteral("%1 — %2").arg(definition.name,state);
}
void ServiceListModel::setDefinitions(QVector<domain::services::ServiceDefinition> definitions){beginResetModel();m_definitions=std::move(definitions);endResetModel();}
void ServiceListModel::setRuntimeStates(QVector<application::services::ServiceRuntimeState> states){m_states.clear();for(auto& state:states)m_states.insert(state.definition.id,std::move(state));if(!m_definitions.isEmpty())emit dataChanged(index(0),index(static_cast<int>(m_definitions.size())-1));}
QString ServiceListModel::serviceId(const QModelIndex& index) const{return data(index,ServiceIdRole).toString();}
}
