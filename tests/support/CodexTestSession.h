#pragma once
#include "infrastructure/codex/CodexClient.h"
#include "infrastructure/codex/CodexRuntimeProfile.h"
#include "MemoryCodexTransport.h"
#include <QJsonArray>
#include <QTemporaryDir>
namespace devhub::tests::codex {
using namespace devhub::application::assistant;
using namespace devhub::infrastructure::codex;
inline QJsonObject safeConfig()
{
    QJsonObject features;
    for (const auto& name : {"shell_tool", "unified_exec", "apps", "plugins", "hooks", "browser_use",
        "computer_use", "goals", "image_generation", "multi_agent", "js_repl", "apply_patch_freeform", "shell_snapshot"})
        features.insert(QLatin1String(name), false);
    return {{"config", QJsonObject{{"features", features}, {"web_search", "disabled"}, {"sandbox_mode", "read-only"},
        {"approval_policy", "on-request"}, {"forced_login_method", "chatgpt"}, {"model_provider", "openai"},
        {"mcp_servers", QJsonObject{}}, {"notify", QJsonArray{}}}}, {"origins", QJsonObject{}},
        {"layers", QJsonArray{QJsonObject{{"name", QJsonObject{{"type", "sessionFlags"}}}, {"config", QJsonObject{}}, {"version", "fixture"}}}}};
}
inline QJsonObject chatGptAccount(const QString& identity = "fixture-account")
{
    return {{"account", QJsonObject{{"type", "chatgpt"}, {"email", QJsonValue(QJsonValue::Null)}, {"planType", "plus"}}},
        {"requiresOpenaiAuth", true}, {"workspaceRouting", QJsonObject{{"backendOrigin", "https://chatgpt.com"},
        {"chatgptAccountId", identity}, {"accountRoutingOverride", "NO_CONSTRAINT"}}}};
}
inline QJsonObject turn(const QString& id = "turn-1", const QString& status = "inProgress", QJsonArray items = {})
{ return {{"id", id}, {"status", status}, {"items", items}, {"error", QJsonValue(QJsonValue::Null)}}; }
inline QJsonObject agent(const QString& id = "agent-1", const QString& text = {})
{ return {{"type", "agentMessage"}, {"id", id}, {"text", text}}; }
struct Session {
    QTemporaryDir scratch;
    MemoryCodexTransport transport;
    CodexClient client;
    explicit Session(CodexClientLimits limits = {}) : client(transport, nullptr, limits) {}
    CodexLaunchSpec spec(quint64 generation = 1, QStringList args = CodexRuntimeProfile::launchArguments().value(),
        QString version = QStringLiteral("0.160.1")) const
    { return {{QStringLiteral("C:/fixture/codex.exe"), version}, args, scratch.path(), QProcessEnvironment{}, generation}; }
    qint64 requestId(const QString& method) const { return transport.lastRequest(method).value("id").toInteger(); }
    void reply(const QString& method, QJsonObject result) { transport.reply(requestId(method), result); }
    bool start()
    {
        if (!client.launch(spec())) return false;
        reply("initialize", {{"codexHome", scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        return client.isInitialized();
    }
    bool ready(QJsonObject account = chatGptAccount())
    {
        if (!start()) return false;
        reply("account/read", account); reply("config/read", safeConfig()); reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        return client.profileVerified();
    }
    QJsonObject thread(const QString& id = "thread-1", QJsonArray turns = {}) const
    {
        return {{"id", id}, {"cliVersion", "0.160.1"}, {"createdAt", 1}, {"updatedAt", 2}, {"cwd", scratch.path()},
            {"ephemeral", false}, {"modelProvider", "openai"}, {"preview", ""}, {"projectId", QJsonValue(QJsonValue::Null)},
            {"sessionId", "session-fixture"}, {"source", "appServer"}, {"status", QJsonObject{{"type", "idle"}}},
            {"turns", turns}, {"environments", QJsonArray{}}, {"historyMode", "legacy"}};
    }
    QJsonObject threadResult(QJsonObject value = {}) const
    {
        return {{"thread", value.isEmpty() ? thread() : value}, {"cwd", scratch.path()}, {"model", "fixture-model"},
            {"modelProvider", "openai"}, {"approvalPolicy", "on-request"}, {"approvalsReviewer", "user"},
            {"sandbox", QJsonObject{{"type", "readOnly"}, {"networkAccess", false}}}, {"runtimeWorkspaceRoots", QJsonArray{}}};
    }
    bool openThread()
    {
        if (!client.startThread("fixture-model")) return false;
        reply("thread/start", threadResult()); return client.threadBinding().threadId == "thread-1";
    }
    bool beginTurn()
    {
        if (!client.startTurn(QString::fromUtf8("Hello İ 🐈"))) return false;
        reply("turn/start", {{"turn", turn()}}); return client.activeTurnId() == "turn-1";
    }
    void item(const QString& method, QJsonObject value)
    { transport.event(method, {{"threadId", "thread-1"}, {"turnId", "turn-1"}, {"item", value}, {method == "item/started" ? "startedAtMs" : "completedAtMs", 1}}); }
    void delta(const QString& text, const QString& itemId = "agent-1", const QString& turnId = "turn-1", const QString& threadId = "thread-1")
    { transport.event("item/agentMessage/delta", {{"threadId", threadId}, {"turnId", turnId}, {"itemId", itemId}, {"delta", text}}); }
};
}

