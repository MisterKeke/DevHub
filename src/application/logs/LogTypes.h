#pragma once

#include <QDateTime>
#include <QString>

namespace devhub::application::logs {

enum class LogSourceKind { File, ServiceStdout, ServiceStderr };
enum class LogSeverity { Trace, Debug, Info, Warning, Error, Critical, Unknown };

struct LogLine final {
    quint64 sequence = 0;
    QDateTime timestamp;
    LogSeverity severity = LogSeverity::Unknown;
    QString text;
};

struct LogSourceConfig final {
    QString id;
    QString workspaceId;
    QString displayName;
    QString filePath;
    QString formatKey = QStringLiteral("text");
    LogSourceKind kind = LogSourceKind::File;
};

[[nodiscard]] QString severityName(LogSeverity severity);
[[nodiscard]] LogLine parseLogLine(quint64 sequence, QString text);

} // namespace devhub::application::logs
