#pragma once
#include "../../domain/services/ServiceDefinition.h"
#include <QObject>
#include <QProcess>

namespace devhub::application::services {
class IManagedProcess : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start() = 0;
    virtual void stop(int timeoutMs) = 0;
    [[nodiscard]] virtual qint64 processId() const = 0;
signals:
    void started();
    void finished(int exitCode, int exitStatus);
    void outputAvailable(QByteArray bytes, bool standardError);
    void processError(QString message);
};
class QProcessManagedProcess final : public IManagedProcess {
    Q_OBJECT
public:
    QProcessManagedProcess(domain::services::ServiceDefinition definition, QObject* parent = nullptr);
    void start() override;
    void stop(int timeoutMs) override;
    [[nodiscard]] qint64 processId() const override;
private:
    domain::services::ServiceDefinition m_definition;
    QProcess m_process;
};
class IProcessLauncher {
public:
    virtual ~IProcessLauncher() = default;
    // Return an unstarted process so callers can connect lifecycle signals first.
    [[nodiscard]] virtual IManagedProcess* launch(const domain::services::ServiceDefinition& definition,
                                                  QObject* parent) = 0;
};
class QProcessLauncher final : public IProcessLauncher {
public:
    IManagedProcess* launch(const domain::services::ServiceDefinition& definition, QObject* parent) override;
};
}
