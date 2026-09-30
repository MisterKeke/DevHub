#include "PortTableModel.h"

#include "../../application/ports/PortMonitorService.h"

namespace devhub::presentation::ports {
using namespace application::ports;

int PortTableModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_records.size()); }
int PortTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant PortTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size()) return {};
    const PortRecord& record = m_records.at(index.row());
    if (role == Qt::UserRole) return QVariant::fromValue(record);
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
    case PortColumn: return record.localPort;
    case ProtocolColumn: return protocolName(record.protocol);
    case ProcessColumn: return record.processName.isEmpty() ? QStringLiteral("Unknown process") : record.processName;
    case PidColumn: return record.process.processId;
    case AddressColumn: return record.localAddress;
    case StateColumn: return record.protocol == PortProtocol::Tcp ? tcpStateName(record.tcpState) : QStringLiteral("—");
    case WorkspaceColumn: return record.associatedWorkspaceName.isEmpty() ? QStringLiteral("—") : record.associatedWorkspaceName;
    default: return {};
    }
}

QVariant PortTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QStringList labels{tr("Port"), tr("Protocol"), tr("Process"), tr("PID"), tr("Local address"), tr("State"), tr("Workspace")};
    return section >= 0 && section < labels.size() ? QVariant(labels.at(section)) : QVariant();
}

void PortTableModel::setRecords(QVector<PortRecord> records)
{
    beginResetModel();
    m_records = std::move(records);
    endResetModel();
}

const PortRecord* PortTableModel::recordAt(int row) const
{
    return row >= 0 && row < m_records.size() ? &m_records.at(row) : nullptr;
}

PortFilterProxyModel::PortFilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void PortFilterProxyModel::setSearchText(QString text)
{
    m_search = std::move(text);
    invalidate();
}

void PortFilterProxyModel::setEndpointFilter(int filter)
{
    m_endpointFilter = filter;
    invalidate();
}

bool PortFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    const PortRecord record = index.data(Qt::UserRole).value<PortRecord>();
    if (m_endpointFilter == 1 && record.protocol != application::ports::PortProtocol::Tcp) return false;
    if (m_endpointFilter == 2 && record.protocol != application::ports::PortProtocol::Udp) return false;
    if (m_endpointFilter == 3 && !(record.protocol == application::ports::PortProtocol::Tcp
                                   && record.tcpState == application::ports::TcpState::Listen)) return false;
    if (m_search.isEmpty()) return true;
    for (int column = 0; column < sourceModel()->columnCount(sourceParent); ++column) {
        if (sourceModel()->index(sourceRow, column, sourceParent).data().toString().contains(m_search, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

} // namespace devhub::presentation::ports
