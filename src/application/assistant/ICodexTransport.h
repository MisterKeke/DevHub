#pragma once

#include "AssistantTypes.h"
#include "shared/common/Result.h"
#include <QJsonObject>
#include <QObject>

namespace devhub::application::assistant {
class ICodexTransport : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void launch(const CodexLaunchSpec& spec) = 0;
    [[nodiscard]] virtual shared::common::Result<void> sendObject(const QJsonObject& object) = 0;
    virtual void stop() = 0;
signals:
    void started(quint64 processGeneration);
    void objectReceived(QJsonObject object, quint64 processGeneration);
    void stopped(int exitCode, bool crashed, quint64 processGeneration);
    void failed(devhub::domain::errors::AppError error, quint64 processGeneration);
};
} // namespace devhub::application::assistant
