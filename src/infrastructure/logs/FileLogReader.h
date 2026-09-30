#pragma once

#include "FileIdentity.h"
#include "../../application/logs/LogDecoder.h"

#include <QFile>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace devhub::infrastructure::logs {

class FileLogReader final : public QObject
{
    Q_OBJECT
public:
    explicit FileLogReader(QObject* parent = nullptr);
    void start(QString path);
    void stop();
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] qint64 byteOffset() const noexcept { return m_offset; }

signals:
    void linesAvailable(QVector<QString> lines);
    void sourceMissing(bool missing);
    void sourceRotated();
    void readFailed(QString message);

private:
    void poll();
    void openCurrentFile(bool fromBeginning);
    void consume(const QByteArray& bytes);
    void emitCompleteLines();
    void updateWatcher();

    QString m_path;
    QFile m_file;
    QFileSystemWatcher m_watcher;
    QTimer m_pollTimer;
    application::logs::LogDecoder m_decoder;
    FileIdentity m_identity;
    QString m_partialLine;
    qint64 m_offset = 0;
    bool m_discardFirstLine = false;
    bool m_missing = false;
};

} // namespace devhub::infrastructure::logs
