#pragma once

#include "LogTypes.h"

#include <QVector>

namespace devhub::application::logs {

class LogRingBuffer final
{
public:
    explicit LogRingBuffer(qsizetype lineLimit = 100000, qsizetype byteLimit = 16 * 1024 * 1024);
    [[nodiscard]] qsizetype append(QVector<LogLine> lines);
    void clear();
    [[nodiscard]] const QVector<LogLine>& lines() const noexcept { return m_lines; }
    [[nodiscard]] qsizetype droppedLineCount() const noexcept { return m_droppedLineCount; }
    [[nodiscard]] qsizetype byteCount() const noexcept { return m_bytes; }
    void setLimits(qsizetype lineLimit, qsizetype byteLimit);

private:
    QVector<LogLine> m_lines;
    qsizetype m_lineLimit;
    qsizetype m_byteLimit;
    qsizetype m_bytes = 0;
    qsizetype m_droppedLineCount = 0;
    quint64 m_nextSequence = 1;
};

} // namespace devhub::application::logs
