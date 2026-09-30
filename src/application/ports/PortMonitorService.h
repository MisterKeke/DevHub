#pragma once

#include "IPortProvider.h"
#include "IProcessInspector.h"

#include <QObject>
#include <QFutureWatcher>

namespace devhub::application::ports {

class PortMonitorService final : public QObject
{
    Q_OBJECT
public:
    PortMonitorService(IPortProvider& provider, IProcessInspector& inspector, QObject* parent = nullptr);
    [[nodiscard]] bool refresh();
    [[nodiscard]] bool isRefreshing() const noexcept { return m_watcher.isRunning(); }
    [[nodiscard]] QVector<PortRecord> records() const { return m_records; }
    [[nodiscard]] shared::common::Result<void> terminate(const PortRecord& record);

signals:
    void refreshStarted();
    void refreshFinished(int count);
    void refreshFailed(QString message);
    void recordsChanged(QVector<devhub::application::ports::PortRecord> records);

private:
    struct RefreshResult final {
        QVector<PortRecord> records;
        QString error;
    };
    IPortProvider& m_provider;
    IProcessInspector& m_inspector;
    QFutureWatcher<RefreshResult> m_watcher;
    QVector<PortRecord> m_records;
};

} // namespace devhub::application::ports

Q_DECLARE_METATYPE(devhub::application::ports::PortRecord)
