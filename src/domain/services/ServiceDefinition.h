#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

namespace devhub::domain::services {
enum class RestartPolicy { Never, OnFailure, Always };
struct ServiceDefinition final {
    QString id;
    QString workspaceId;
    QString name;
    QString program;
    QStringList arguments;
    QString workingDirectory;
    bool shellMode = false;
    QStringList environmentNames;
    QVector<quint16> expectedPorts;
    RestartPolicy restartPolicy = RestartPolicy::Never;
    int stopTimeoutMs = 5000;
};
}
