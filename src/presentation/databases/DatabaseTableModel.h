#pragma once

#include "../../application/databases/DatabaseTypes.h"

#include <QAbstractTableModel>

namespace devhub::presentation::databases {

class DatabaseTableModel final : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit DatabaseTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    void setPage(QString tableName, application::databases::DatabasePageResult page,
                 QVector<application::databases::DatabaseColumn> columns);
    void setEditMode(bool enabled);
    [[nodiscard]] bool hasStableIdentity() const noexcept { return !m_primaryKeyColumns.isEmpty(); }
    [[nodiscard]] bool hasEditMode() const noexcept { return m_editMode; }
    [[nodiscard]] QString tableName() const { return m_tableName; }
    [[nodiscard]] QVector<application::databases::DatabaseColumn> columnMetadata() const { return m_columns; }
    [[nodiscard]] int pageOffset() const noexcept { return m_offset; }
    [[nodiscard]] int nextPageOffset() const noexcept { return m_nextOffset; }
    [[nodiscard]] bool hasMore() const noexcept { return m_hasMore; }
    [[nodiscard]] bool hasPendingChanges() const noexcept;
    void appendBlankRow();
    void deleteRows(QModelIndexList rows);
    [[nodiscard]] application::databases::PendingChanges pendingChanges() const;

private:
    [[nodiscard]] QVector<QVariant> identityFor(int row) const;
    [[nodiscard]] QVariant typedValue(const QVariant& value, int column) const;
    QString m_tableName;
    QStringList m_headers;
    QVector<application::databases::DatabaseColumn> m_columns;
    QVector<QVector<QVariant>> m_rows;
    QVector<QVector<QVariant>> m_originalRows;
    QVector<int> m_primaryKeyColumns;
    QVector<bool> m_insertRows;
    QVector<application::databases::PendingCellEdit> m_edits;
    QVector<application::databases::PendingRowDelete> m_deletes;
    int m_offset = 0;
    int m_nextOffset = 0;
    bool m_hasMore = false;
    bool m_editMode = false;
};

} // namespace devhub::presentation::databases
