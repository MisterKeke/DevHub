#include "LogLineModel.h"

#include <QRegularExpression>

namespace devhub::presentation::logs {
using namespace application::logs;

int LogLineModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_lines.size()); }

QVariant LogLineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size()) return {};
    const LogLine& line = m_lines.at(index.row());
    if (role == Qt::DisplayRole) return QStringLiteral("%1  [%2]  %3")
        .arg(line.timestamp.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz")), severityName(line.severity), line.text);
    if (role == Qt::ToolTipRole) return line.text;
    if (role == SequenceRole) return QVariant::fromValue<qulonglong>(line.sequence);
    if (role == SeverityRole) return static_cast<int>(line.severity);
    if (role == TimestampRole) return line.timestamp;
    return {};
}

void LogLineModel::setLines(QVector<LogLine> lines)
{
    beginResetModel(); m_lines = std::move(lines); endResetModel();
}

void LogLineModel::appendLines(QVector<LogLine> lines)
{
    if (lines.isEmpty()) return;
    const int first = static_cast<int>(m_lines.size());
    const int last = first + static_cast<int>(lines.size()) - 1;
    beginInsertRows({}, first, last);
    for (auto& line : lines) m_lines.append(std::move(line));
    endInsertRows();
}

const LogLine* LogLineModel::lineAt(int row) const { return row >= 0 && row < m_lines.size() ? &m_lines.at(row) : nullptr; }

LogFilterProxyModel::LogFilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterKeyColumn(0);
    setDynamicSortFilter(true);
}

void LogFilterProxyModel::setSeverityFilter(LogSeverity severity)
{
    m_filterSeverity = severity != LogSeverity::Unknown;
    m_severity = severity;
    invalidate();
}

bool LogFilterProxyModel::setTextFilter(const QString& text, bool regularExpression)
{
    if (regularExpression) {
        const QRegularExpression expression(text, QRegularExpression::CaseInsensitiveOption);
        if (!expression.isValid()) return false;
        setFilterRegularExpression(expression);
    } else {
        setFilterRegularExpression(QRegularExpression(QRegularExpression::escape(text), QRegularExpression::CaseInsensitiveOption));
    }
    return true;
}

bool LogFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (!QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent)) return false;
    if (!m_filterSeverity) return true;
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    return index.data(LogLineModel::SeverityRole).toInt() == static_cast<int>(m_severity);
}

} // namespace devhub::presentation::logs
