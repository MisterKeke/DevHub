#pragma once

#include "../../application/ports/PortTypes.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

namespace devhub::presentation::ports {

class PortTableModel final : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { PortColumn, ProtocolColumn, ProcessColumn, PidColumn, AddressColumn, StateColumn, WorkspaceColumn, ColumnCount };
    explicit PortTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void setRecords(QVector<application::ports::PortRecord> records);
    [[nodiscard]] const application::ports::PortRecord* recordAt(int row) const;

private:
    QVector<application::ports::PortRecord> m_records;
};

class PortFilterProxyModel final : public QSortFilterProxyModel
{
public:
    explicit PortFilterProxyModel(QObject* parent = nullptr);
    void setSearchText(QString text);
    void setEndpointFilter(int filter);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString m_search;
    int m_endpointFilter = 0;
};

} // namespace devhub::presentation::ports
