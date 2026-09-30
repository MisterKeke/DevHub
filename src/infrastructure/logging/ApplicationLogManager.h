#pragma once
#include "../../shared/security/RedactionService.h"
#include <QString>
#include <QMutex>
#include <QLoggingCategory>

namespace devhub::infrastructure::logging {
class ApplicationLogManager final {
public:
    explicit ApplicationLogManager(QString directory);
    ~ApplicationLogManager();
    ApplicationLogManager(const ApplicationLogManager&) = delete;
    ApplicationLogManager& operator=(const ApplicationLogManager&) = delete;
    [[nodiscard]] const QString& directory() const noexcept { return m_directory; }
    [[nodiscard]] const shared::security::RedactionService& redactor() const noexcept { return m_redactor; }
    [[nodiscard]] QString logFilePath() const;
private:
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
    void write(QtMsgType type, const QMessageLogContext& context, const QString& message);
    void rotateIfNeeded();
    static ApplicationLogManager* s_active;
    static QtMessageHandler s_previousHandler;
    QString m_directory;
    shared::security::RedactionService m_redactor;
    QMutex m_mutex;
};
} // namespace devhub::infrastructure::logging
