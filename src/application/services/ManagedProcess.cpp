#include "ManagedProcess.h"
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>

namespace devhub::application::services {
namespace {
QString shellCommand(const QStringList& parts) {
    QStringList quoted;
    for (QString part : parts) {
        part.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        if (part.contains(QRegularExpression(QStringLiteral("\\s")))) part = QLatin1Char('"') + part + QLatin1Char('"');
        quoted.append(std::move(part));
    }
    return quoted.join(QLatin1Char(' '));
}
}
QProcessManagedProcess::QProcessManagedProcess(domain::services::ServiceDefinition definition, QObject* parent)
    : IManagedProcess(parent), m_definition(std::move(definition)) {
    connect(&m_process, &QProcess::started, this, &IManagedProcess::started);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { emit outputAvailable(m_process.readAllStandardOutput(), false); });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { emit outputAvailable(m_process.readAllStandardError(), true); });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) { emit finished(code, static_cast<int>(status)); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        emit processError(QStringLiteral("QProcess error %1: %2").arg(static_cast<int>(error)).arg(m_process.errorString()));
    });
}
void QProcessManagedProcess::start() {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto& name : m_definition.environmentNames) {
        if (environment.contains(name)) environment.insert(name, environment.value(name));
    }
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(m_definition.workingDirectory);
    if (!m_definition.shellMode) {
        m_process.setProgram(m_definition.program); m_process.setArguments(m_definition.arguments);
    } else {
#ifdef Q_OS_WIN
        m_process.setProgram(QStringLiteral("cmd.exe"));
        m_process.setArguments({QStringLiteral("/S"), QStringLiteral("/C"), shellCommand(QStringList{m_definition.program} + m_definition.arguments)});
#else
        m_process.setProgram(QStringLiteral("/bin/sh"));
        m_process.setArguments({QStringLiteral("-c"), shellCommand(QStringList{m_definition.program} + m_definition.arguments)});
#endif
    }
    m_process.start();
}
void QProcessManagedProcess::stop(int timeoutMs) {
    if (m_process.state() == QProcess::NotRunning) return;
    m_process.terminate();
    QTimer::singleShot(qBound(100, timeoutMs, 120000), this, [this] {
        if (m_process.state() != QProcess::NotRunning) m_process.kill();
    });
}
qint64 QProcessManagedProcess::processId() const { return static_cast<qint64>(m_process.processId()); }
IManagedProcess* QProcessLauncher::launch(const domain::services::ServiceDefinition& definition, QObject* parent) {
    return new QProcessManagedProcess(definition, parent);
}
}
