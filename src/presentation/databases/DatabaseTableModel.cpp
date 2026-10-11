#include "DatabaseTableModel.h"

#include <algorithm>
#include <QFont>

namespace devhub::presentation::databases {
using namespace application::databases;
namespace {
bool sameCellValue(const QVariant& left, const QVariant& right)
{
    return left.isValid() == right.isValid() && left.isNull() == right.isNull() && left == right;
}
}

int DatabaseTableModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
int DatabaseTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_headers.size()); }

QVariant DatabaseTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()
        || index.column() < 0 || index.column() >= m_headers.size()) return {};
    const QVariant value = m_rows.at(index.row()).at(index.column());
    if (role == Qt::EditRole) return value;
    if (role == Qt::FontRole && value.isNull()) { QFont font; font.setItalic(true); return font; }
    if (role == Qt::ToolTipRole) {
        if (value.isNull()) return tr("SQL NULL");
        if (value.metaType().id() == QMetaType::QByteArray) return tr("BLOB (%1 bytes)").arg(value.toByteArray().size());
        if (!m_insertRows.at(index.row()) && m_rows.at(index.row()) != m_originalRows.value(index.row()))
            return tr("Edited; changes are pending until Commit Changes.");
        return value.isNull() ? tr("SQL NULL") : value.toString();
    }
    if (role != Qt::DisplayRole) return {};
    if (!value.isValid() || value.isNull()) return tr("(SQL NULL)");
    if (value.metaType().id() == QMetaType::QByteArray) return tr("[BLOB, %1 bytes]").arg(value.toByteArray().size());
    return value.toString();
}

QVariant DatabaseTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < m_headers.size()) {
        QString label = m_headers.at(section);
        if (m_primaryKeyColumns.contains(section)) label += QStringLiteral(" 🔑");
        return label;
    }
    if (orientation == Qt::Vertical && role == Qt::DisplayRole) return m_offset + section + 1;
    return {};
}

Qt::ItemFlags DatabaseTableModel::flags(const QModelIndex& index) const
{
    auto result = QAbstractTableModel::flags(index);
    if (m_editMode && hasStableIdentity() && index.isValid() && !m_primaryKeyColumns.contains(index.column())) {
        const QVariant value = m_rows.at(index.row()).at(index.column());
        const QString display = value.toString();
        const bool previewOnly = value.metaType().id() == QMetaType::QByteArray
            || display.startsWith(QStringLiteral("[BLOB,"))
            || display.contains(QStringLiteral("… [truncated]"));
        if (!previewOnly) result |= Qt::ItemIsEditable;
    }
    if (m_editMode && hasStableIdentity() && index.isValid() && m_insertRows.at(index.row())) result |= Qt::ItemIsEditable;
    return result;
}

QVariant DatabaseTableModel::typedValue(const QVariant& value, int column) const
{
    if (!value.isValid() || value.isNull()) return value;
    if (column < 0 || column >= m_columns.size()) return value;
    const QString type = m_columns.at(column).declaredType.toUpper();
    const QString text = value.toString();
    if (type.contains(QStringLiteral("INT"))) {
        bool ok = false; const qlonglong number = text.toLongLong(&ok); if (ok) return number;
    } else if (type.contains(QStringLiteral("REAL")) || type.contains(QStringLiteral("FLOA")) || type.contains(QStringLiteral("DOUB")) || type.contains(QStringLiteral("NUM"))) {
        bool ok = false; const double number = text.toDouble(&ok); if (ok) return number;
    }
    return value;
}

bool DatabaseTableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || index.model() != this || !(flags(index) & Qt::ItemIsEditable)) return false;
    if (!m_insertRows.at(index.row())) {
        const QVariant current = m_rows.at(index.row()).at(index.column());
        if (current.toString().startsWith(QStringLiteral("[BLOB,"))
            || current.toString().contains(QStringLiteral("… [truncated]"))) return false;
    }
    const QVariant converted = typedValue(value, index.column());
    if (sameCellValue(m_rows[index.row()][index.column()], converted)) return true;
    m_rows[index.row()][index.column()] = converted;
    if (!m_insertRows.at(index.row())) {
        const auto key = identityFor(index.row());
        const QString columnName = m_headers.at(index.column());
        auto it = std::find_if(m_edits.begin(), m_edits.end(), [&key, &columnName](const PendingCellEdit& edit) {
            return edit.primaryKey == key && edit.columnName == columnName;
        });
        const QVariant original = m_originalRows.at(index.row()).at(index.column());
        if (sameCellValue(converted, original)) {
            if (it != m_edits.end()) m_edits.erase(it);
        } else if (it != m_edits.end()) it->value = converted;
        else m_edits.append({key, columnName, converted, original});
    }
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole, Qt::FontRole});
    return true;
}

void DatabaseTableModel::setPage(QString tableName, DatabasePageResult page, QVector<DatabaseColumn> columns)
{
    beginResetModel();
    m_tableName = std::move(tableName); m_headers = std::move(page.columns); m_rows = std::move(page.rows);
    m_originalRows = m_rows; m_primaryKeyColumns = std::move(page.primaryKeyColumns); m_columns = std::move(columns);
    m_offset = page.offset; m_hasMore = page.hasMore; m_insertRows.clear();
    m_nextOffset = page.offset + static_cast<int>(m_rows.size());
    m_insertRows.resize(m_rows.size()); std::fill(m_insertRows.begin(), m_insertRows.end(), false);
    m_edits.clear(); m_deletes.clear();
    endResetModel();
}

void DatabaseTableModel::setEditMode(bool enabled) { m_editMode = enabled; }
bool DatabaseTableModel::hasPendingChanges() const noexcept { return !m_edits.isEmpty() || !m_deletes.isEmpty() || std::any_of(m_insertRows.cbegin(), m_insertRows.cend(), [](bool inserted) { return inserted; }); }

QVector<QVariant> DatabaseTableModel::identityFor(int row) const
{
    QVector<QVariant> identity;
    if (row < 0 || row >= m_originalRows.size()) return identity;
    for (const int column : m_primaryKeyColumns) identity.append(m_originalRows.at(row).at(column));
    return identity;
}

void DatabaseTableModel::appendBlankRow()
{
    const int row = static_cast<int>(m_rows.size());
    beginInsertRows({}, row, row);
    m_rows.append(QVector<QVariant>(m_headers.size())); m_originalRows.append(QVector<QVariant>{}); m_insertRows.append(true);
    endInsertRows();
}

void DatabaseTableModel::deleteRows(QModelIndexList selected)
{
    QVector<int> rows;
    for (const auto& index : selected) if (index.isValid() && !rows.contains(index.row())) rows.append(index.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (const int row : rows) {
        if (row < 0 || row >= m_rows.size()) continue;
        if (!m_insertRows.at(row)) {
            const auto key = identityFor(row);
            if (std::none_of(m_deletes.cbegin(), m_deletes.cend(), [&key](const PendingRowDelete& item) { return item.primaryKey == key; })) m_deletes.append({key});
            m_edits.erase(std::remove_if(m_edits.begin(), m_edits.end(), [&key](const PendingCellEdit& edit) { return edit.primaryKey == key; }), m_edits.end());
        }
        beginRemoveRows({}, row, row); m_rows.removeAt(row); m_originalRows.removeAt(row); m_insertRows.removeAt(row); endRemoveRows();
    }
}

PendingChanges DatabaseTableModel::pendingChanges() const
{
    PendingChanges changes; changes.tableName = m_tableName; changes.cellEdits = m_edits; changes.deletes = m_deletes;
    for (qsizetype row = 0; row < m_rows.size(); ++row) if (m_insertRows.at(row)) {
        PendingRowInsert insert;
        for (qsizetype column = 0; column < m_headers.size(); ++column) if (m_rows.at(row).at(column).isValid())
            insert.values.insert(m_headers.at(column), m_rows.at(row).at(column));
        changes.inserts.append(std::move(insert));
    }
    return changes;
}

} // namespace devhub::presentation::databases
