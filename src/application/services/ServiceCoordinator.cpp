#include "ServiceCoordinator.h"
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>
#include <memory>

namespace devhub::application::services {
ServiceCoordinator::ServiceCoordinator(IServiceDefinitionRepository& repository, IProcessLauncher& launcher, QObject* parent)
    : QObject(parent), m_repository(repository), m_launcher(launcher) {}
shared::common::Result<void> ServiceCoordinator::start(domain::services::ServiceDefinition definition, QString workspaceRoot) {
    const QString id = definition.id;
    if (id.isEmpty() || (m_processes.contains(id) && m_processes.value(id))) return shared::common::Result<void>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Process,
            QStringLiteral("This service is already starting or running."), QStringLiteral("Duplicate managed process prevented."), QStringLiteral("start service")));
    auto validated = m_validator.validate(std::move(definition), workspaceRoot);
    if (!validated) return shared::common::Result<void>::failure(validated.error());
    auto saved = m_repository.save(validated.value());
    if (!saved) return saved;
    ServiceRuntimeState state; state.definition = validated.value(); state.lifecycle = ServiceLifecycleState::Starting;
    state.health = state.definition.expectedPorts.isEmpty() ? ServiceHealthState::Unknown : ServiceHealthState::Checking;
    setState(id, state);
    IManagedProcess* process = m_launcher.launch(state.definition, this);
    m_processes.insert(id, process);
    m_startOrder.removeAll(id); m_startOrder.append(id);
    connect(process, &IManagedProcess::started, this, [this, id, process] {
        if (m_processes.value(id) != process) return;
        if (m_states.value(id).lifecycle == ServiceLifecycleState::Stopping) return;
        auto current = m_states.value(id); current.lifecycle = ServiceLifecycleState::Running;
        current.processId = process->processId(); current.startedAt = QDateTime::currentDateTimeUtc();
        if (current.definition.expectedPorts.isEmpty()) current.health = ServiceHealthState::Healthy;
        setState(id, current);
        if (!current.definition.expectedPorts.isEmpty()) probeExpectedPorts(id);
    });
    connect(process, &IManagedProcess::outputAvailable, this,
        [this, id](QByteArray bytes, bool standardError) { emit outputAvailable(id, std::move(bytes), standardError); });
    connect(process, &IManagedProcess::processError, this, [this, id, process](const QString& message) {
        if (m_processes.value(id) != process) return;
        auto current = m_states.value(id); current.error = message;
        const bool stopping = current.lifecycle == ServiceLifecycleState::Stopping;
        const bool failedToStart = (current.lifecycle == ServiceLifecycleState::Starting || stopping) && process->processId() == 0;
        if (failedToStart) {
            current.lifecycle = stopping ? ServiceLifecycleState::Stopped : ServiceLifecycleState::Failed;
            m_processes.remove(id);
            process->deleteLater();
        }
        setState(id, current);
        if (failedToStart && !stopping && current.definition.restartPolicy != domain::services::RestartPolicy::Never)
            scheduleRestart(id);
    });
    connect(process, &IManagedProcess::finished, this, [this, id, process](int code, int exitStatus) {
        if (m_processes.value(id) != process) return;
        auto current = m_states.value(id); current.exitCode = code; current.exitStatus = exitStatus;
        current.processId = 0;
        const bool expected = current.lifecycle == ServiceLifecycleState::Stopping;
        current.lifecycle = expected ? ServiceLifecycleState::Stopped : (code == 0 ? ServiceLifecycleState::Stopped : ServiceLifecycleState::Failed);
        if (!expected && code != 0) current.error = QStringLiteral("Process exited with code %1.").arg(code);
        m_processes.remove(id); setState(id, current); process->deleteLater();
        if (!expected && (current.definition.restartPolicy == domain::services::RestartPolicy::Always
            || (current.definition.restartPolicy == domain::services::RestartPolicy::OnFailure && code != 0))) scheduleRestart(id);
    });
    QTimer::singleShot(15000, this, [this, id, process = QPointer<IManagedProcess>(process)] {
        if (process && m_processes.value(id) == process
            && m_states.value(id).lifecycle == ServiceLifecycleState::Starting && process->processId() == 0) {
            auto current = m_states.value(id); current.lifecycle = ServiceLifecycleState::Failed;
            current.error = QStringLiteral("Process did not start within 15 seconds.");
            m_processes.remove(id); process->stop(100); process->deleteLater(); setState(id, current);
        }
    });
    process->start();
    return shared::common::Result<void>::success();
}
shared::common::Result<void> ServiceCoordinator::stop(const QString& serviceId) {
    ++m_restartGeneration[serviceId];
    auto process = m_processes.value(serviceId);
    if (!process) return shared::common::Result<void>::success();
    auto state = m_states.value(serviceId); state.lifecycle = ServiceLifecycleState::Stopping; setState(serviceId, state);
    process->stop(state.definition.stopTimeoutMs); return shared::common::Result<void>::success();
}
shared::common::Result<void> ServiceCoordinator::restart(const QString& serviceId, QString workspaceRoot) {
    const auto it = m_states.constFind(serviceId);
    if (it == m_states.cend()) return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Process,
        QStringLiteral("Service definition was not found."), serviceId, QStringLiteral("restart service")));
    auto definition = it->definition;
    if (m_processes.contains(serviceId) && m_processes.value(serviceId)) {
        const quint64 generation = m_restartGeneration.value(serviceId) + 1;
        connect(m_processes.value(serviceId), &IManagedProcess::finished, this,
            [this, definition, workspaceRoot, generation](int, int) mutable {
                if (m_restartGeneration.value(definition.id) != generation) return;
                const auto result=start(std::move(definition), std::move(workspaceRoot));
                if (!result) qWarning().noquote()<<result.error().technicalMessage;
            }, Qt::SingleShotConnection);
        return stop(serviceId);
    }
    return start(std::move(definition), std::move(workspaceRoot));
}
QVector<ServiceRuntimeState> ServiceCoordinator::states() const { return m_states.values().toVector(); }
bool ServiceCoordinator::hasRunningServices() const {
    for (auto it=m_states.cbegin(); it!=m_states.cend(); ++it)
        if (it->lifecycle==ServiceLifecycleState::Starting || it->lifecycle==ServiceLifecycleState::Running || it->lifecycle==ServiceLifecycleState::Stopping) return true;
    return false;
}
void ServiceCoordinator::setState(const QString& id, ServiceRuntimeState state) { m_states.insert(id,state); emit stateChanged(id,state); }
void ServiceCoordinator::scheduleRestart(const QString& id) {
    const int attempts = m_restartAttempts.value(id);
    if (attempts >= 5) return;
    m_restartAttempts.insert(id, attempts+1);
    const int delay = qMin(30000, 1000 * (1 << attempts));
    const quint64 generation = ++m_restartGeneration[id];
    QTimer::singleShot(delay, this, [this,id,generation] {
        if (m_restartGeneration.value(id) != generation) return;
        if (m_processes.contains(id) && m_processes.value(id)) return;
        const auto state=m_states.value(id); const auto result=start(state.definition, state.definition.workingDirectory);
        if (!result) qWarning().noquote()<<result.error().technicalMessage;
    });
}
void ServiceCoordinator::startAll(QVector<domain::services::ServiceDefinition> definitions, QString workspaceRoot, int maximumConcurrent) {
    maximumConcurrent=qBound(1,maximumConcurrent,8);
    auto pending=std::make_shared<QVector<domain::services::ServiceDefinition>>(std::move(definitions));
    auto pump=std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakPump=pump;
    const quint64 generation = ++m_startAllGeneration;
    *pump=[this,pending,weakPump,workspaceRoot=std::move(workspaceRoot),maximumConcurrent,generation]() mutable {
        if (m_startAllGeneration != generation) return;
        int active=0; for (auto it=m_states.cbegin();it!=m_states.cend();++it)
            if(it->lifecycle==ServiceLifecycleState::Starting)++active;
        while(active<maximumConcurrent&&!pending->isEmpty()) {
            auto definition=pending->takeFirst();
            if (m_processes.value(definition.id)) continue;
            const auto result=start(std::move(definition),workspaceRoot);
            if(!result) qWarning().noquote()<<result.error().technicalMessage; else ++active;
        }
        if(!pending->isEmpty()) {
            if(auto next=weakPump.lock()) QTimer::singleShot(100,this,[next]{(*next)();});
        }
    };
    (*pump)();
}
void ServiceCoordinator::stopAll() {
    ++m_startAllGeneration;
    const auto startOrder = m_startOrder;
    for(auto it=startOrder.crbegin();it!=startOrder.crend();++it) {
        const auto result=stop(*it); if(!result) qWarning().noquote()<<result.error().technicalMessage;
    }
}
void ServiceCoordinator::probeExpectedPorts(const QString& id, int attempt) {
    const auto runtime=m_states.value(id);
    if (runtime.lifecycle!=ServiceLifecycleState::Running) return;
    const auto ports=runtime.definition.expectedPorts;
    if (ports.isEmpty()) return;
    struct ProbeBatch { int pending=0; int open=0; int total=0; };
    auto batch=std::make_shared<ProbeBatch>(); batch->pending=static_cast<int>(ports.size()); batch->total=static_cast<int>(ports.size());
    for (const quint16 port:ports) {
        auto* socket=new QTcpSocket(this); auto completed=std::make_shared<bool>(false);
        auto finish=[this,id,attempt,batch,completed,socket](bool isOpen) {
            if (*completed) return;
            *completed=true; if (isOpen) ++batch->open; --batch->pending;
            socket->abort(); socket->deleteLater();
            if (batch->pending!=0) return;
            auto current=m_states.value(id);
            current.health=batch->open==batch->total ? ServiceHealthState::Healthy : ServiceHealthState::Checking;
            setState(id,current);
            if (batch->open!=batch->total) {
                if (attempt<14) QTimer::singleShot(1000,this,[this,id,attempt]{probeExpectedPorts(id,attempt+1);});
                else { current=m_states.value(id);current.health=ServiceHealthState::Unhealthy;setState(id,current); }
            }
        };
        connect(socket,&QTcpSocket::connected,this,[finish]{finish(true);});
        connect(socket,&QTcpSocket::errorOccurred,this,[finish](QAbstractSocket::SocketError){finish(false);});
        QTimer::singleShot(900,socket,[socket,finish]{if(socket->state()!=QAbstractSocket::UnconnectedState){socket->abort();finish(false);}});
        socket->connectToHost(QHostAddress::LocalHost,port);
    }
}
}
