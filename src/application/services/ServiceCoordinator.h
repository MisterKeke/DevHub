#pragma once
#include "IServiceDefinitionRepository.h"
#include "ManagedProcess.h"
#include "ServiceDefinitionValidator.h"
#include "ServiceTypes.h"
#include <QObject>
#include <QHash>
#include <QPointer>
#include <functional>

namespace devhub::application::services {
class ServiceCoordinator final : public QObject {
    Q_OBJECT
public:
    ServiceCoordinator(IServiceDefinitionRepository& repository, IProcessLauncher& launcher, QObject* parent = nullptr);
    [[nodiscard]] shared::common::Result<void> start(domain::services::ServiceDefinition definition, QString workspaceRoot);
    [[nodiscard]] shared::common::Result<void> stop(const QString& serviceId);
    [[nodiscard]] shared::common::Result<void> restart(const QString& serviceId, QString workspaceRoot,
        std::function<bool()> replacementAuthorized = {});
    [[nodiscard]] quint64 nextActionIdentity() const { return m_actionIdentity + 1; }
    void startAll(QVector<domain::services::ServiceDefinition> definitions, QString workspaceRoot, int maximumConcurrent = 3);
    void stopAll();
    [[nodiscard]] QVector<ServiceRuntimeState> states() const;
    [[nodiscard]] bool hasRunningServices() const;
    void cancelPendingRestart(const QString& serviceId, quint64 actionIdentity = 0);
signals:
    void stateChanged(QString serviceId, devhub::application::services::ServiceRuntimeState state);
    void outputAvailable(QString serviceId, QByteArray bytes, bool standardError);
private:
    [[nodiscard]] shared::common::Result<void> startWithAction(domain::services::ServiceDefinition,
        QString workspaceRoot, quint64 actionIdentity);
    [[nodiscard]] shared::common::Result<void> stopWithAction(const QString& serviceId, quint64 actionIdentity);
    void setState(const QString& id, ServiceRuntimeState state);
    void scheduleRestart(const QString& id);
    void probeExpectedPorts(const QString& id, int attempt = 0);
    IServiceDefinitionRepository& m_repository;
    IProcessLauncher& m_launcher;
    ServiceDefinitionValidator m_validator;
    QHash<QString, ServiceRuntimeState> m_states;
    QHash<QString, QPointer<IManagedProcess>> m_processes;
    QHash<QString, int> m_restartAttempts;
    QHash<QString, quint64> m_restartGeneration;
    QHash<QString, quint64> m_cancelledRestartProcesses;
    QHash<QString, quint64> m_serviceActions;
    quint64 m_startAllGeneration = 0;
    quint64 m_processGeneration = 0;
    quint64 m_actionIdentity = 0;
    QStringList m_startOrder;
};
}
