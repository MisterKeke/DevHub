#pragma once

#include "LogLineModel.h"
#include "../../application/logs/ILogSourceRepository.h"
#include "../../application/logs/LogRingBuffer.h"
#include "../../application/logs/LogDecoder.h"

#include <QWidget>
#include <QHash>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QTimer;

namespace devhub::application::services { class ServiceCoordinator; }
namespace devhub::infrastructure::logs { class FileLogReader; }

namespace devhub::presentation::logs {

class LogsPage final : public QWidget
{
    Q_OBJECT
public:
    LogsPage(application::logs::ILogSourceRepository& repository,
             application::services::ServiceCoordinator& services,
             QWidget* parent = nullptr, qsizetype lineLimit = 10000, qsizetype byteLimit = 4 * 1024 * 1024);
    void setWorkspace(QString workspaceId, QString workspacePath);
    void focusService(const QString& serviceId);

private:
    struct StreamState final {
        application::logs::LogDecoder decoder;
        QString partial;
    };
    void reloadSources();
    void selectSource(const QString& sourceId);
    void addFileSource();
    void removeSelectedSource();
    void appendToSource(const QString& sourceId, QVector<application::logs::LogLine> lines);
    void appendServiceOutput(const QString& serviceId, const QByteArray& bytes, bool standardError);
    void refreshDisplay();
    [[nodiscard]] QString selectedSourceId() const;

    application::logs::ILogSourceRepository& m_repository;
    application::services::ServiceCoordinator& m_services;
    QString m_workspaceId;
    QString m_workspacePath;
    QVector<application::logs::LogSourceConfig> m_fileSources;
    QHash<QString, application::logs::LogRingBuffer> m_buffers;
    QHash<QString, std::shared_ptr<StreamState>> m_streams;
    QHash<QString, QString> m_serviceNames;
    infrastructure::logs::FileLogReader* m_fileReader = nullptr;
    QListView* m_linesView = nullptr;
    LogLineModel* m_model = nullptr;
    LogFilterProxyModel* m_proxy = nullptr;
    QComboBox* m_sources = nullptr;
    QLineEdit* m_filter = nullptr;
    QComboBox* m_severity = nullptr;
    QCheckBox* m_regex = nullptr;
    QCheckBox* m_pause = nullptr;
    QLabel* m_status = nullptr;
    QTimer* m_filterTimer = nullptr;
    bool m_clearedView = false;
    QString m_activeReaderSourceId;
    qsizetype m_lineLimit;
    qsizetype m_byteLimit;
};

} // namespace devhub::presentation::logs
