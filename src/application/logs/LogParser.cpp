#include "LogParser.h"

#include <QRegularExpression>

namespace devhub::application::logs {
namespace {
const QRegularExpression& severityPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("(?:^|\\b)(TRACE|DEBUG|INFO|WARN(?:ING)?|ERROR|FATAL|CRITICAL)(?:\\b|:)") ,
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

const QRegularExpression& timestampPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}[T ][0-9:.+-]+Z?)"));
    return pattern;
}
}

QString severityName(LogSeverity severity)
{
    switch (severity) {
    case LogSeverity::Trace: return QStringLiteral("Trace");
    case LogSeverity::Debug: return QStringLiteral("Debug");
    case LogSeverity::Info: return QStringLiteral("Info");
    case LogSeverity::Warning: return QStringLiteral("Warning");
    case LogSeverity::Error: return QStringLiteral("Error");
    case LogSeverity::Critical: return QStringLiteral("Critical");
    case LogSeverity::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

LogLine parseLogLine(quint64 sequence, QString text) { return LogParser::parse(sequence, std::move(text)); }

LogLine LogParser::parse(quint64 sequence, QString text)
{
    LogLine line;
    line.sequence = sequence;
    line.timestamp = QDateTime::currentDateTimeUtc();
    line.text = std::move(text);
    const auto timestamp = timestampPattern().match(line.text);
    if (timestamp.hasMatch()) {
        const QString value = timestamp.captured(1);
        QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
        if (!parsed.isValid()) parsed = QDateTime::fromString(value, Qt::ISODate);
        if (parsed.isValid()) line.timestamp = parsed.toUTC();
    }
    const auto severity = severityPattern().match(line.text);
    if (severity.hasMatch()) {
        const QString value = severity.captured(1).toUpper();
        if (value == QStringLiteral("TRACE")) line.severity = LogSeverity::Trace;
        else if (value == QStringLiteral("DEBUG")) line.severity = LogSeverity::Debug;
        else if (value == QStringLiteral("INFO")) line.severity = LogSeverity::Info;
        else if (value.startsWith(QStringLiteral("WARN"))) line.severity = LogSeverity::Warning;
        else if (value == QStringLiteral("ERROR")) line.severity = LogSeverity::Error;
        else line.severity = LogSeverity::Critical;
    }
    return line;
}

} // namespace devhub::application::logs
