#pragma once
#include "CodexTestSession.h"
#include "application/assistant/AssistantController.h"
namespace devhub::tests::assistant {
class MemoryConversationRepository final : public application::assistant::IConversationRepository {
public:
    shared::common::Result<QList<application::assistant::AssistantConversationMetadata>> list(const QString& accountId, int limit) override {
        if (failList) return shared::common::Result<QList<application::assistant::AssistantConversationMetadata>>::failure(
            domain::errors::AppError::failure(domain::errors::ErrorCode::AssistantHistoryUnavailable,
                domain::errors::ErrorCategory::Persistence, QStringLiteral("fixture list failure"), {}, QStringLiteral("test list")));
        QList<application::assistant::AssistantConversationMetadata> result;
        for (auto it = rows.crbegin(); it != rows.crend() && result.size() < limit; ++it)
            if (it->devhubAccountId == accountId) result.append(*it);
        return shared::common::Result<QList<application::assistant::AssistantConversationMetadata>>::success(result);
    }
    shared::common::Result<void> save(const application::assistant::AssistantConversationMetadata& metadata) override {
        if (failSave) return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::DatabaseQueryFailed, domain::errors::ErrorCategory::Persistence,
            QStringLiteral("fixture save failure"), {}, QStringLiteral("test save")));
        for (auto& row : rows) if (row.conversation.id == metadata.conversation.id) { row = metadata; return shared::common::Result<void>::success(); }
        rows.append(metadata); return shared::common::Result<void>::success();
    }
    QList<application::assistant::AssistantConversationMetadata> rows;
    bool failList = false;
    bool failSave = false;
};
class FixedClock final : public shared::time::IApplicationClock {
public:
    QDateTime utcNow() const override { return QDateTime::fromString(QStringLiteral("2026-10-09T12:00:00Z"), Qt::ISODate); }
};
class SequenceIds final : public shared::identifiers::IIdGenerator {
public:
    QUuid create() override { return QUuid(QStringLiteral("00000000-0000-4000-8000-%1").arg(++m_next, 12, 16, QLatin1Char('0'))); }
private:
    quint64 m_next = 0;
};
struct Harness : codex::Session {
    FixedClock clock;
    SequenceIds ids;
    QString runtimeVersion;
    application::assistant::AssistantController controller;
    explicit Harness(application::assistant::AssistantLimits limits = {}, application::assistant::CodexClientLimits clientLimits = {},
        application::assistant::IConversationRepository* repository = nullptr, QString version = QStringLiteral("0.160.1"))
        : codex::Session(clientLimits), runtimeVersion(std::move(version)), controller(client, clock, ids, scratch.path(), infrastructure::codex::CodexRuntimeProfile::launchArguments().value(), {}, repository, nullptr, limits)
    {
        QObject::connect(&controller, &application::assistant::AssistantController::executableProbeRequested, &controller,
            [this](const QString&, quint64 generation) { controller.executableFound({"C:/fixture/codex.exe", runtimeVersion}, generation); });
    }
    bool readyController()
    {
        controller.open();
        reply("initialize", {{"codexHome", scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        reply("account/read", codex::chatGptAccount()); reply("config/read", codex::safeConfig());
        reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        reply("model/list", {{"data", QJsonArray{QJsonObject{{"id", "fixture"}, {"model", "fixture-model"}, {"displayName", "Fixture"},
            {"description", "Synthetic model"}, {"defaultReasoningEffort", "medium"}, {"supportedReasoningEfforts", QJsonArray{}},
            {"hidden", false}, {"isDefault", true}, {"inputModalities", QJsonArray{"text"}}}}}});
        return client.isInitialized() && client.profileVerified();
    }
    bool sendAndAccept(const QString& text = "Hello")
    {
        if (!controller.send(text)) return false;
        if (controller.operation() == application::assistant::AssistantOperation::CreatingThread) reply("thread/start", threadResult());
        reply("turn/start", {{"turn", codex::turn()}});
        return controller.operation() == application::assistant::AssistantOperation::Streaming;
    }
    void finish(const QString& status = "completed", QJsonArray items = {})
    { transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", codex::turn("turn-1", status, items)}}); }
};
}
