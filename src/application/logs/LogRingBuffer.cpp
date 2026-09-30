#include "LogRingBuffer.h"

#include <QtGlobal>

namespace devhub::application::logs {

LogRingBuffer::LogRingBuffer(qsizetype lineLimit, qsizetype byteLimit)
    : m_lineLimit(qMax<qsizetype>(1, lineLimit)), m_byteLimit(qMax<qsizetype>(1, byteLimit))
{
}

qsizetype LogRingBuffer::append(QVector<LogLine> lines)
{
    for (LogLine& line : lines) {
        line.sequence = m_nextSequence++;
        if (!line.timestamp.isValid()) line.timestamp = QDateTime::currentDateTimeUtc();
        if (line.text.size() > m_byteLimit / 2) line.text = line.text.left(m_byteLimit / 2) + QStringLiteral(" … [line compacted]");
        m_bytes += line.text.toUtf8().size();
        m_lines.append(std::move(line));
    }
    qsizetype dropped = 0;
    while (!m_lines.isEmpty() && (m_lines.size() > m_lineLimit || m_bytes > m_byteLimit)) {
        m_bytes -= m_lines.constFirst().text.toUtf8().size();
        m_lines.removeFirst();
        ++dropped;
    }
    m_droppedLineCount += dropped;
    return dropped;
}

void LogRingBuffer::clear()
{
    m_lines.clear();
    m_bytes = 0;
    m_droppedLineCount = 0;
}

void LogRingBuffer::setLimits(qsizetype lineLimit, qsizetype byteLimit)
{
    m_lineLimit = qMax<qsizetype>(1, lineLimit);
    m_byteLimit = qMax<qsizetype>(1, byteLimit);
    (void)append({});
}

} // namespace devhub::application::logs
