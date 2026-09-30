#include "WorkspaceListModel.h"

namespace devhub::presentation::workspaces {

int WorkspaceListModel::rowCount(const QModelIndex& parent) const
{ return parent.isValid() ? 0 : static_cast<int>(m_visible.size()); }

QVariant WorkspaceListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const auto& workspace = m_all.at(m_visible.at(index.row()));
    switch (role) {
    case Qt::DisplayRole: return workspace.displayName();
    case Qt::ToolTipRole: return workspace.path().enteredPath();
    case WorkspaceIdRole: return workspace.id().toString();
    case NameRole: return workspace.displayName();
    case PathRole: return workspace.path().enteredPath();
    case StatusRole: return static_cast<int>(workspace.status());
    case FavoriteRole: return workspace.isFavorite();
    case RecencyRole: return workspace.lastOpenedAtUtc().has_value()
            ? workspace.lastOpenedAtUtc()->toString(Qt::ISODateWithMs) : QString();
    default: return {};
    }
}

QHash<int, QByteArray> WorkspaceListModel::roleNames() const
{ return {{WorkspaceIdRole, "workspaceId"}, {NameRole, "name"}, {PathRole, "path"},
          {StatusRole, "status"}, {FavoriteRole, "favorite"}, {RecencyRole, "recency"}}; }

void WorkspaceListModel::setWorkspaces(QVector<domain::workspace::Workspace> workspaces)
{
    beginResetModel(); m_all = std::move(workspaces); rebuildFilter(); endResetModel();
}

const domain::workspace::Workspace* WorkspaceListModel::workspaceAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) return nullptr;
    return &m_all.at(m_visible.at(row));
}

int WorkspaceListModel::rowForId(const QString& id) const
{
    for (int row = 0; row < m_visible.size(); ++row)
        if (m_all.at(m_visible.at(row)).id().toString() == id) return row;
    return -1;
}

void WorkspaceListModel::setFilter(QString text)
{
    const QString previous = m_filter; m_filter = std::move(text);
    if (m_filter == previous) return;
    beginResetModel(); rebuildFilter(); endResetModel();
}

void WorkspaceListModel::rebuildFilter()
{
    m_visible.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        const auto& workspace = m_all.at(i);
        if (m_filter.isEmpty() || workspace.displayName().contains(m_filter, Qt::CaseInsensitive)
            || workspace.path().enteredPath().contains(m_filter, Qt::CaseInsensitive)) m_visible.append(i);
    }
}

} // namespace devhub::presentation::workspaces
