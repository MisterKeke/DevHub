#pragma once

#include "../../application/logs/LogTypes.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>

namespace devhub::presentation::logs {

class LogLineModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { SequenceRole = Qt::UserRole + 1, SeverityRole, TimestampRole };
    explicit LogLineModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    void setLines(QVector<application::logs::LogLine> lines);
    void appendLines(QVector<application::logs::LogLine> lines);
    [[nodiscard]] const application::logs::LogLine* lineAt(int row) const;

private:
    QVector<application::logs::LogLine> m_lines;
};

class LogFilterProxyModel final : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit LogFilterProxyModel(QObject* parent = nullptr);
    void setSeverityFilter(application::logs::LogSeverity severity);
    [[nodiscard]] bool setTextFilter(const QString& text, bool regularExpression);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    application::logs::LogSeverity m_severity = application::logs::LogSeverity::Unknown;
    bool m_filterSeverity = false;
};

} // namespace devhub::presentation::logs
