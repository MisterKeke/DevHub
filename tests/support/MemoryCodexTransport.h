#pragma once
#include "application/assistant/ICodexTransport.h"
#include <functional>

class MemoryCodexTransport final : public devhub::application::assistant::ICodexTransport {
public:
    using ICodexTransport::ICodexTransport;
    quint64 generation = 0;
    int stops = 0;
    bool rejectWrites = false;
    bool native = false;
    QList<QJsonObject> sent;
    struct RecordedLaunch { QStringList arguments; QProcessEnvironment environment; QString scratchDirectory; quint64 processGeneration; };
    QList<RecordedLaunch> launches;
    std::function<void(QJsonObject)> onSend;
    [[nodiscard]] devhub::application::assistant::CodexTransportKind kind() const override
    { return native ? devhub::application::assistant::CodexTransportKind::NativeProcess : devhub::application::assistant::CodexTransportKind::InMemoryTest; }
    void launch(const devhub::application::assistant::CodexLaunchSpec& spec) override
    { launches.append({spec.arguments, spec.environment, spec.scratchDirectory, spec.processGeneration}); generation = spec.processGeneration; emit started(generation); }
    [[nodiscard]] devhub::shared::common::Result<void> sendObject(const QJsonObject& object) override
    {
        if (rejectWrites) return devhub::shared::common::Result<void>::failure(devhub::domain::errors::AppError::failure(
            devhub::domain::errors::ErrorCode::IoFailure, devhub::domain::errors::ErrorCategory::Process, "Synthetic write failure", {}));
        sent.append(object); if (onSend) onSend(object);
        return devhub::shared::common::Result<void>::success();
    }
    void stop() override { ++stops; emit stopped(0, false, generation); }
    void deliver(QJsonObject object) { emit objectReceived(object, generation); }
    void reply(qint64 id, QJsonObject result) { deliver({{"id", id}, {"result", result}}); }
    void event(const QString& method, QJsonObject params) { deliver({{"method", method}, {"params", params}}); }
    [[nodiscard]] QJsonObject lastRequest(const QString& method) const
    {
        for (auto it = sent.crbegin(); it != sent.crend(); ++it) if (it->value("method") == method) return *it;
        return {};
    }
};
