#pragma once

#include "../../domain/workspace/Workspace.h"

#include <QAbstractListModel>
#include <QVector>

namespace devhub::presentation::workspaces {

class WorkspaceListModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { WorkspaceIdRole = Qt::UserRole + 1, NameRole, PathRole, StatusRole, FavoriteRole, RecencyRole };
    explicit WorkspaceListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setWorkspaces(QVector<domain::workspace::Workspace> workspaces);
    [[nodiscard]] const domain::workspace::Workspace* workspaceAt(int row) const;
    [[nodiscard]] int rowForId(const QString& id) const;
    void setFilter(QString text);

private:
    QVector<domain::workspace::Workspace> m_all;
    QVector<int> m_visible;
    QString m_filter;
    void rebuildFilter();
};

} // namespace devhub::presentation::workspaces
