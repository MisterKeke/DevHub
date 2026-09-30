#include "ApplicationLogManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>

namespace devhub::infrastructure::logging {
ApplicationLogManager* ApplicationLogManager::s_active = nullptr;
QtMessageHandler ApplicationLogManager::s_previousHandler = nullptr;

ApplicationLogManager::ApplicationLogManager(QString directory) : m_directory(std::move(directory))
{
    QSet<QString> secrets;
    static const QRegularExpression secretPattern(QStringLiteral("(?i)(PASSWORD|PASSWD|TOKEN|SECRET|API[_-]?KEY|PRIVATE[_-]?KEY)"));
    const auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto& key : environment.keys()) if (secretPattern.match(key).hasMatch()) {
        secrets.insert(key);
        m_redactor.addSecretEnvironmentValue(environment.value(key));
    }
    m_redactor.setSecretEnvironmentNames(std::move(secrets));
    QDir().mkpath(m_directory);
    s_active = this;
    s_previousHandler = qInstallMessageHandler(&ApplicationLogManager::messageHandler);
}

ApplicationLogManager::~ApplicationLogManager()
{
    if (s_active == this) {
        qInstallMessageHandler(s_previousHandler);
        s_active = nullptr;
        s_previousHandler = nullptr;
    }
}

QString ApplicationLogManager::logFilePath() const { return QDir(m_directory).filePath(QStringLiteral("devhub.log")); }

void ApplicationLogManager::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (s_active) s_active->write(type, context, message);
    if (s_previousHandler) {
        const QString safe = s_active ? s_active->m_redactor.redact(message) : message;
        s_previousHandler(type, context, safe);
    }
}

void ApplicationLogManager::write(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QMutexLocker locker(&m_mutex);
    rotateIfNeeded();
    QFile file(logFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    const char* severity = type == QtDebugMsg ? "DEBUG" : type == QtInfoMsg ? "INFO" :
        type == QtWarningMsg ? "WARNING" : type == QtCriticalMsg ? "CRITICAL" : "FATAL";
    const QString category = context.category ? QString::fromUtf8(context.category) : QStringLiteral("default");
    const auto threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    const QString line = QStringLiteral("%1 %2 [%3] [thread:%4] [correlation:none] %5\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), QString::fromLatin1(severity).leftJustified(8),
             category, QString::number(threadId, 16), m_redactor.redact(message));
    file.write(line.toUtf8());
    file.flush();
    if (type == QtFatalMsg) file.close();
}

void ApplicationLogManager::rotateIfNeeded()
{
    constexpr qint64 maxBytes = 5 * 1024 * 1024;
    constexpr int retained = 4;
    const QString current = logFilePath();
    if (QFileInfo(current).size() < maxBytes) return;
    for (int index = retained - 1; index >= 1; --index) {
        const QString from = QDir(m_directory).filePath(QStringLiteral("devhub.%1.log").arg(index));
        const QString to = QDir(m_directory).filePath(QStringLiteral("devhub.%1.log").arg(index + 1));
        if (QFile::exists(to)) QFile::remove(to);
        if (QFile::exists(from)) QFile::rename(from, to);
    }
    const QString first = QDir(m_directory).filePath(QStringLiteral("devhub.1.log"));
    if (QFile::exists(first)) QFile::remove(first);
    QFile::rename(current, first);
}
} // namespace devhub::infrastructure::logging
