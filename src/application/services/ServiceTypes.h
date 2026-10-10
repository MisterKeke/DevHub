#pragma once
#include "../../domain/services/ServiceDefinition.h"
#include <QDateTime>

namespace devhub::application::services {
enum class ServiceLifecycleState { Stopped, Starting, Running, Stopping, Failed };
enum class ServiceHealthState { Unknown, Checking, Healthy, Unhealthy };
struct ServiceRuntimeState final {
    domain::services::ServiceDefinition definition;
    ServiceLifecycleState lifecycle = ServiceLifecycleState::Stopped;
    ServiceHealthState health = ServiceHealthState::Unknown;
    qint64 processId = 0;
    quint64 generation = 0;
    quint64 actionIdentity = 0;
    QDateTime startedAt;
    int exitCode = -1;
    int exitStatus = 0;
    QString error;
};
}
