#include "FileLogReader.h"

#include <QFileInfo>

namespace devhub::infrastructure::logs {
namespace {
constexpr qint64 InitialTailBytes = 1024 * 1024;
constexpr qint64 MaximumBytesPerPoll = 1024 * 1024;
constexpr qint64 ChunkSize = 64 * 1024;
}

FileLogReader::FileLogReader(QObject* parent) : QObject(parent)
{
    m_pollTimer.setInterval(1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &FileLogReader::poll);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { poll(); updateWatcher(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { poll(); updateWatcher(); });
}

void FileLogReader::start(QString path)
{
    stop();
    m_path = std::move(path);
    openCurrentFile(false);
    m_pollTimer.start();
    updateWatcher();
}

void FileLogReader::stop()
{
    m_pollTimer.stop();
    const QStringList files = m_watcher.files();
    if (!files.isEmpty()) m_watcher.removePaths(files);
    const QStringList directories = m_watcher.directories();
    if (!directories.isEmpty()) m_watcher.removePaths(directories);
    m_file.close();
    m_path.clear();
    m_identity = {};
    m_partialLine.clear();
    m_offset = 0;
    m_discardFirstLine = false;
    m_decoder.reset();
}

void FileLogReader::openCurrentFile(bool fromBeginning)
{
    m_file.close();
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        const bool missingNow = !QFileInfo::exists(m_path);
        if (missingNow != m_missing) emit sourceMissing(missingNow);
        m_missing = missingNow;
        if (!missingNow) emit readFailed(m_file.errorString());
        m_identity = {};
        return;
    }
    const FileIdentity nextIdentity = identifyFile(m_path);
    if (m_identity.isValid() && nextIdentity.isValid() && nextIdentity != m_identity) emit sourceRotated();
    m_identity = nextIdentity;
    const qint64 size = m_file.size();
    const qint64 start = (!fromBeginning && size > InitialTailBytes) ? size - InitialTailBytes : 0;
    if (!m_file.seek(start)) {
        emit readFailed(m_file.errorString());
        return;
    }
    m_offset = start;
    m_discardFirstLine = start > 0;
    m_partialLine.clear();
    m_decoder.reset();
    if (m_missing) emit sourceMissing(false);
    m_missing = false;
    poll();
}

void FileLogReader::poll()
{
    if (m_path.isEmpty()) return;
    if (!QFileInfo::exists(m_path)) {
        if (!m_missing) emit sourceMissing(true);
        m_missing = true;
        return;
    }
    const FileIdentity currentIdentity = identifyFile(m_path);
    if (!m_file.isOpen() || !currentIdentity.isValid() || currentIdentity != m_identity) {
        openCurrentFile(true);
        updateWatcher();
        return;
    }
    const qint64 size = QFileInfo(m_path).size();
    if (size < m_offset) {
        m_file.seek(0);
        m_offset = 0;
        m_partialLine.clear();
        m_decoder.reset();
        emit sourceRotated();
    }
    QByteArray batch;
    qint64 readThisPoll = 0;
    while (readThisPoll < MaximumBytesPerPoll) {
        const QByteArray chunk = m_file.read(qMin(ChunkSize, MaximumBytesPerPoll - readThisPoll));
        if (chunk.isEmpty()) break;
        m_offset += chunk.size();
        readThisPoll += chunk.size();
        batch.append(chunk);
    }
    if (!batch.isEmpty()) consume(batch);
}

void FileLogReader::consume(const QByteArray& bytes)
{
    m_partialLine.append(m_decoder.decode(bytes));
    constexpr qsizetype partialLimit = 1024 * 1024;
    if (m_partialLine.size() > partialLimit) m_partialLine = QStringLiteral("[long line compacted] ") + m_partialLine.right(partialLimit);
    emitCompleteLines();
}

void FileLogReader::emitCompleteLines()
{
    QVector<QString> lines;
    qsizetype start = 0;
    for (qsizetype index = 0; index < m_partialLine.size(); ++index) {
        if (m_partialLine.at(index) != QLatin1Char('\n')) continue;
        QString line = m_partialLine.mid(start, index - start);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (m_discardFirstLine) m_discardFirstLine = false;
        else lines.append(std::move(line));
        start = index + 1;
    }
    if (start > 0) m_partialLine.remove(0, start);
    if (!lines.isEmpty()) emit linesAvailable(std::move(lines));
}

void FileLogReader::updateWatcher()
{
    if (m_path.isEmpty()) return;
    if (QFileInfo::exists(m_path) && !m_watcher.files().contains(m_path)) m_watcher.addPath(m_path);
    const QString directory = QFileInfo(m_path).absolutePath();
    if (QFileInfo(directory).isDir() && !m_watcher.directories().contains(directory)) m_watcher.addPath(directory);
}

} // namespace devhub::infrastructure::logs
