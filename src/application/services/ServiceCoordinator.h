#pragma once
#include "IServiceDefinitionRepository.h"
#include "ManagedProcess.h"
#include "ServiceDefinitionValidator.h"
#include "ServiceTypes.h"
#include <QObject>
#include <QHash>
#include <QPointer>

namespace devhub::application::services {
class ServiceCoordinator final : public QObject {
    Q_OBJECT
public:
    ServiceCoordinator(IServiceDefinitionRepository& repository, IProcessLauncher& launcher, QObject* parent = nullptr);
    [[nodiscard]] shared::common::Result<void> start(domain::services::ServiceDefinition definition, QString workspaceRoot);
    [[nodiscard]] shared::common::Result<void> stop(const QString& serviceId);
    [[nodiscard]] shared::common::Result<void> restart(const QString& serviceId, QString workspaceRoot);
    void startAll(QVector<domain::services::ServiceDefinition> definitions, QString workspaceRoot, int maximumConcurrent = 3);
    void stopAll();
    [[nodiscard]] QVector<ServiceRuntimeState> states() const;
    [[nodiscard]] bool hasRunningServices() const;
signals:
    void stateChanged(QString serviceId, devhub::application::services::ServiceRuntimeState state);
    void outputAvailable(QString serviceId, QByteArray bytes, bool standardError);
private:
    void setState(const QString& id, ServiceRuntimeState state);
    void scheduleRestart(const QString& id);
    void probeExpectedPorts(const QString& id, int attempt = 0);
    IServiceDefinitionRepository& m_repository;
    IProcessLauncher& m_launcher;
    ServiceDefinitionValidator m_validator;
    QHash<QString, ServiceRuntimeState> m_states;
    QHash<QString, QPointer<IManagedProcess>> m_processes;
    QHash<QString, int> m_restartAttempts;
    QStringList m_startOrder;
};
}
