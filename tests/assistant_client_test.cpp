#include "infrastructure/codex/CodexClient.h"
#include "infrastructure/codex/CodexRuntimeProfile.h"
#include "support/MemoryCodexTransport.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace devhub::application::assistant;
using namespace devhub::infrastructure::codex;
using devhub::domain::errors::ErrorCode;
namespace {
QJsonObject safeConfig()
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
QJsonObject chatGptAccount(const QString& identity = "fixture-account")
{
    return {{"account", QJsonObject{{"type", "chatgpt"}, {"email", QJsonValue(QJsonValue::Null)}, {"planType", "plus"}}},
        {"requiresOpenaiAuth", true}, {"workspaceRouting", QJsonObject{{"backendOrigin", "https://chatgpt.com"},
        {"chatgptAccountId", identity}, {"accountRoutingOverride", "NO_CONSTRAINT"}}}};
}
QJsonObject turn(const QString& id = "turn-1", const QString& status = "inProgress", QJsonArray items = {})
{ return {{"id", id}, {"status", status}, {"items", items}, {"error", QJsonValue(QJsonValue::Null)}}; }
QJsonObject agent(const QString& id = "agent-1", const QString& text = {})
{ return {{"type", "agentMessage"}, {"id", id}, {"text", text}}; }
struct Session {
    QTemporaryDir scratch;
    MemoryCodexTransport transport;
    CodexClient client;
    explicit Session(CodexClientLimits limits = {}) : client(transport, nullptr, limits) {}
    CodexLaunchSpec spec(quint64 generation = 1, QStringList args = CodexRuntimeProfile::launchArguments().value()) const
    { return {{QStringLiteral("C:/fixture/codex.exe"), QStringLiteral("0.160.1")}, args, scratch.path(), QProcessEnvironment{}, generation}; }
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
class AssistantClientTest final : public QObject {
    Q_OBJECT
private slots:
    void handshakeAndRequiredFields()
    {
        Session s; QSignalSpy initialized(&s.client, &CodexClient::initialized);
        QVERIFY(s.client.launch(s.spec()));
        const auto initialize = s.transport.lastRequest("initialize");
        QCOMPARE(initialize.value("params").toObject().value("clientInfo").toObject(),
            (QJsonObject{{"name", "devhub"}, {"title", "DevHub"}, {"version", DEVHUB_VERSION}}));
        QVERIFY(initialize.value("params").toObject().value("capabilities").toObject().value("experimentalApi").toBool());
        QCOMPARE(s.transport.sent.size(), 1); QVERIFY(!s.client.isInitialized()); QVERIFY(!s.client.startThread("fixture-model"));
        emit s.transport.started(s.transport.generation); QCOMPARE(s.transport.sent.size(), 1);
        s.reply("initialize", {{"codexHome", s.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        QCOMPARE(initialized.size(), 1); QCOMPARE(s.transport.sent.at(1), (QJsonObject{{"method", "initialized"}}));
        QCOMPARE(s.client.pendingRequestCount(), 3); QVERIFY(!s.client.canSend());
        QCOMPARE(s.transport.lastRequest("account/read").value("params").toObject(), (QJsonObject{{"refreshToken", false}}));
        QCOMPARE(s.transport.lastRequest("config/read").value("params").toObject().value("includeLayers"), QJsonValue(true));
        QVERIFY(s.transport.lastRequest("configRequirements/read").value("params").isNull());
        s.transport.reply(initialize.value("id").toInteger(), {}); QCOMPARE(initialized.size(), 1);
    }
    void failedInitializeNeverSendsInitialized()
    {
        Session s; QSignalSpy failed(&s.client, &CodexClient::requestFailed);
        QVERIFY(s.client.launch(s.spec()));
        s.transport.deliver({{"id", s.requestId("initialize")}, {"error", QJsonObject{{"code", -32600}, {"message", "secret remote details"}}}});
        QCOMPARE(failed.size(), 1); QVERIFY(!s.client.isInitialized()); QCOMPARE(s.transport.sent.size(), 1);
        QVERIFY(!qvariant_cast<devhub::domain::errors::AppError>(failed.front().at(2)).userMessage.contains("secret"));
    }
    void malformedRequiredResponse()
    {
        Session s; QSignalSpy failed(&s.client, &CodexClient::protocolFailed);
        QVERIFY(s.client.launch(s.spec())); s.reply("initialize", {{"userAgent", "fixture"}});
        QCOMPARE(failed.size(), 1); QCOMPARE(s.transport.stops, 1); QVERIFY(!s.client.isInitialized());
    }
    void authModes_data()
    {
        QTest::addColumn<QJsonObject>("account"); QTest::addColumn<int>("mode");
        QTest::newRow("null requires auth") << QJsonObject{{"account", QJsonValue(QJsonValue::Null)}, {"requiresOpenaiAuth", true}} << int(CodexAccountMode::SignedOut);
        QTest::newRow("null custom provider") << QJsonObject{{"account", QJsonValue(QJsonValue::Null)}, {"requiresOpenaiAuth", false}} << int(CodexAccountMode::SignedOut);
        QTest::newRow("API key") << QJsonObject{{"account", QJsonObject{{"type", "apiKey"}}}, {"requiresOpenaiAuth", true}} << int(CodexAccountMode::ApiKey);
        QTest::newRow("Bedrock") << QJsonObject{{"account", QJsonObject{{"type", "amazonBedrock"}}}, {"requiresOpenaiAuth", false}} << int(CodexAccountMode::Other);
        QTest::newRow("ChatGPT") << chatGptAccount() << int(CodexAccountMode::ChatGpt);
    }
    void authModes()
    {
        QFETCH(QJsonObject, account); QFETCH(int, mode); Session s;
        QVERIFY(s.ready(account)); QCOMPARE(int(s.client.account().mode), mode);
        if (mode == int(CodexAccountMode::ChatGpt)) { QVERIFY(!s.client.account().identityFingerprint.isEmpty()); QVERIFY(s.openThread()); }
        else QVERIFY(!s.client.startThread("fixture-model"));
    }
    void noRoutingNoRestoreOrSend()
    {
        Session s; auto account = chatGptAccount(); account.remove("workspaceRouting"); QVERIFY(s.ready(account));
        QCOMPARE(s.client.account().mode, CodexAccountMode::ChatGpt); QVERIFY(s.client.account().identityFingerprint.isEmpty());
        QVERIFY(!s.client.startThread("fixture-model")); QVERIFY(!s.client.canSend());
    }
    void profileCannotBeAdmittedByInitializeOrCallerFlags()
    {
        Session s; QVERIFY(s.start()); s.reply("account/read", chatGptAccount());
        QVERIFY(!s.client.startThread("fixture-model")); s.reply("config/read", safeConfig()); QVERIFY(!s.client.profileVerified());
        s.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}}); QVERIFY(s.client.profileVerified()); QVERIFY(s.openThread());
        QVERIFY(s.client.canSend()); s.transport.native = true; QVERIFY(!s.client.canSend()); QVERIFY(!s.client.startTurn("hello"));
        QVERIFY(!CodexRuntimeProfile::liveInferenceApproved());
    }
    void admissionRejectsUnsafeProfiles_data()
    {
        QTest::addColumn<QString>("variant");
        for (const auto& name : {"shell", "mcp", "notify", "managed", "unknown layer", "missing layers", "attached", "provider", "missing features"}) QTest::newRow(name) << QString::fromLatin1(name);
    }
    void admissionRejectsUnsafeProfiles()
    {
        QFETCH(QString, variant); Session s; QVERIFY(s.start()); s.reply("account/read", chatGptAccount());
        auto configResult = safeConfig(); auto config = configResult.value("config").toObject(); QJsonObject requirements;
        if (variant == "shell") { auto features = config.value("features").toObject(); features.insert("shell_tool", true); config.insert("features", features); }
        if (variant == "mcp") config.insert("mcp_servers", QJsonObject{{"inherited", QJsonObject{}}});
        if (variant == "notify") config.insert("notify", QJsonArray{"unreviewed.exe"});
        if (variant == "managed") requirements.insert("hooks", QJsonObject{{"SessionStart", QJsonArray{QJsonObject{}}}});
        if (variant == "attached") config.insert("desktop", QJsonObject{{"attached", true}});
        if (variant == "provider") config.insert("model_provider", "custom");
        if (variant == "missing features") config.remove("features");
        if (variant == "unknown layer") configResult.insert("layers", QJsonArray{QJsonObject{{"name", QJsonObject{{"type", "enterpriseManaged"}, {"id", "fixture"}, {"name", "fixture"}}}, {"config", QJsonObject{}}, {"version", "1"}}});
        if (variant == "missing layers") configResult.remove("layers");
        configResult.insert("config", config); s.reply("config/read", configResult); s.reply("configRequirements/read", {{"requirements", requirements}});
        QVERIFY(!s.client.profileVerified()); QVERIFY(!s.client.startThread("fixture-model")); QVERIFY(!s.client.startTurn("hello"));
    }
    void launchOverrideValidationAndMcpQuoting()
    {
        Session s; auto args = CodexRuntimeProfile::launchArguments({"name.with.dots", "quote\"name", "native İ"}); QVERIFY(args);
        QVERIFY(CodexRuntimeProfile::acceptsLaunch(args.value())); QVERIFY(args.value().contains("mcp_servers.\"name.with.dots\".enabled=false"));
        QVERIFY(!CodexRuntimeProfile::launchArguments({"bad\nname"}));
        auto unsafe = args.value(); unsafe << "-c" << "features.shell_tool=true"; QVERIFY(!s.client.launch(s.spec(1, unsafe)));
        QCOMPARE(s.transport.sent.size(), 0); QVERIFY(s.client.launch(s.spec(1, args.value())));
    }
    void threadAndTurnExactRequests()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.client.canSend());
        const auto start = s.transport.lastRequest("thread/start").value("params").toObject();
        QCOMPARE(start.value("environments"), QJsonValue(QJsonArray{})); QCOMPARE(start.value("historyMode"), QJsonValue("legacy"));
        QCOMPARE(start.value("sandbox"), QJsonValue("read-only")); QCOMPARE(start.value("dynamicTools"), QJsonValue(QJsonArray{}));
        QVERIFY(s.beginTurn()); QVERIFY(!s.client.startTurn("duplicate"));
        const auto params = s.transport.lastRequest("turn/start").value("params").toObject();
        QCOMPARE(params.value("threadId"), QJsonValue("thread-1")); QCOMPARE(params.value("environments"), QJsonValue(QJsonArray{}));
        QCOMPARE(params.value("input").toArray().at(0).toObject().value("text"), QJsonValue(QString::fromUtf8("Hello İ 🐈")));
        QVERIFY(s.client.interruptTurn());
        QCOMPARE(s.transport.lastRequest("turn/interrupt").value("params").toObject(), (QJsonObject{{"threadId", "thread-1"}, {"turnId", "turn-1"}}));
        s.reply("turn/interrupt", {}); QCOMPARE(s.client.activeTurnId(), QStringLiteral("turn-1")); QVERIFY(!s.client.interruptTurn());
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", turn("turn-1", "interrupted")}});
        QVERIFY(s.client.activeTurnId().isEmpty()); QVERIFY(s.client.canSend());
    }
    void idCollisionDirectionsAndTypes()
    {
        Session s; QVERIFY(s.ready()); QSignalSpy models(&s.client, &CodexClient::modelsReceived);
        const auto pending = s.client.listModels(); QVERIFY(pending); const auto id = pending.value();
        const auto before = s.transport.sent.size();
        s.transport.deliver({{"id", id}, {"method", "unknown/execute"}, {"params", QJsonObject{{"secret", "hidden"}}}});
        QCOMPARE(s.client.pendingRequestCount(), 1); QCOMPARE(s.transport.sent.size(), before + 1);
        QCOMPARE(s.transport.sent.back().value("id"), QJsonValue(id)); QCOMPARE(s.transport.sent.back().value("error").toObject().value("code"), QJsonValue(-32601));
        s.transport.deliver({{"id", QString::number(id)}, {"result", QJsonObject{{"data", QJsonArray{}}}}}); QCOMPARE(models.size(), 0);
        s.transport.reply(id, {{"data", QJsonArray{}}}); QCOMPARE(models.size(), 1);
        s.transport.reply(id, {{"data", QJsonArray{}}}); QCOMPARE(models.size(), 1);
        s.transport.deliver({{"id", QString::number(id)}, {"method", "unknown/execute"}, {"params", QJsonObject{}}});
        QVERIFY(s.transport.sent.back().value("id").isString());
    }
    void duplicateAndConflictingServerRequest()
    {
        Session s; QVERIFY(s.ready()); const QJsonObject request{{"id", "server-id"}, {"method", "unknown"}, {"params", QJsonObject{}}};
        s.transport.deliver(request); const auto size = s.transport.sent.size(); s.transport.deliver(request); QCOMPARE(s.transport.sent.size(), size);
        auto conflicting = request; conflicting.insert("method", "other"); QSignalSpy failed(&s.client, &CodexClient::protocolFailed);
        s.transport.deliver(conflicting); QCOMPARE(failed.size(), 1); QVERIFY(!s.client.isInitialized());
    }
    void disabledServerRequests_data()
    {
        QTest::addColumn<QString>("method"); QTest::addColumn<QString>("contract");
        QTest::newRow("native command") << QStringLiteral("item/commandExecution/requestApproval") << QStringLiteral("Response:CommandExecutionRequestApprovalResponse");
        QTest::newRow("native file") << QStringLiteral("item/fileChange/requestApproval") << QStringLiteral("Response:FileChangeRequestApprovalResponse");
        QTest::newRow("permissions") << QStringLiteral("item/permissions/requestApproval") << QStringLiteral("Response:PermissionsRequestApprovalResponse");
        QTest::newRow("legacy command") << QStringLiteral("execCommandApproval") << QStringLiteral("Response:ExecCommandApprovalResponse");
        QTest::newRow("legacy patch") << QStringLiteral("applyPatchApproval") << QStringLiteral("Response:ApplyPatchApprovalResponse");
        QTest::newRow("dynamic tool") << QStringLiteral("item/tool/call") << QStringLiteral("Response:DynamicToolCallResponse");
        QTest::newRow("native questions") << QStringLiteral("item/tool/requestUserInput") << QStringLiteral("");
        QTest::newRow("MCP elicitation") << QStringLiteral("mcpServer/elicitation/request") << QStringLiteral("");
        QTest::newRow("external token refresh") << QStringLiteral("account/chatgptAuthTokens/refresh") << QStringLiteral("");
    }
    void disabledServerRequests()
    {
        QFETCH(QString, method); QFETCH(QString, contract); Session s; QVERIFY(s.ready());
        s.transport.deliver({{"id", "native-request"}, {"method", method}, {"params", QJsonObject{}}}); const auto response = s.transport.sent.back();
        QCOMPARE(response.value("id"), QJsonValue("native-request")); CodexWireContract wire;
        if (contract.isEmpty()) QCOMPARE(response.value("error").toObject().value("code"), QJsonValue(-32601));
        else QVERIFY(wire.accepts(contract, response.value("result")));
        QVERIFY(!response.value("result").toObject().value("success").toBool());
    }
    void earlyTurnEventsWaitForAcknowledgement()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QSignalSpy accepted(&s.client, &CodexClient::turnAccepted), items(&s.client, &CodexClient::itemChanged), finished(&s.client, &CodexClient::turnFinished);
        const auto pending = s.client.startTurn("hello"); QVERIFY(pending);
        s.delta("unrelated", "agent-foreign", "turn-foreign"); s.delta("other thread", "agent-foreign", "turn-1", "thread-foreign");
        s.item("item/started", agent()); s.delta("Hello "); s.delta(QString::fromUtf8("🐈")); s.item("item/completed", agent("agent-1", QString::fromUtf8("Hello 🐈!")));
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", turn("turn-1", "completed", QJsonArray{agent("agent-1", QString::fromUtf8("Hello 🐈!"))})}});
        QCOMPARE(items.size(), 0); QCOMPARE(finished.size(), 0);
        s.transport.reply(pending.value(), {{"turn", turn()}}); QCOMPARE(accepted.size(), 1); QCOMPARE(finished.size(), 1);
        const auto result = qvariant_cast<CodexTurn>(finished.front().at(0)); QCOMPARE(result.items.size(), 1); QCOMPARE(result.items.front().text, QString::fromUtf8("Hello 🐈!"));
        QCOMPARE(result.status, CodexTurnStatus::Completed); QVERIFY(s.client.canSend());
        const auto count = items.size(); s.item("item/completed", agent("agent-1", "duplicate")); s.transport.reply(pending.value(), {{"turn", turn()}});
        QCOMPARE(items.size(), count); QCOMPARE(finished.size(), 1); QVERIFY(s.client.ignoredMessageCount() >= 4);
    }
    void multipleItemsFinalTextAndMismatches()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.beginTurn()); QSignalSpy changed(&s.client, &CodexClient::itemChanged), finished(&s.client, &CodexClient::turnFinished);
        s.delta("prefix", "commentary"); s.item("item/started", agent("commentary")); s.item("item/completed", agent("commentary", "reconciled"));
        const auto count = changed.size(); s.item("item/completed", agent("commentary", "reconciled")); s.delta("late", "commentary"); QCOMPARE(changed.size(), count);
        s.delta("bad", "stray", "turn-wrong"); s.delta("bad", "stray", "turn-1", "thread-wrong");
        s.item("item/started", agent("answer")); s.delta("answer text", "answer");
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", turn("turn-1", "completed", QJsonArray{agent("answer", "final")})}});
        QCOMPARE(finished.size(), 1); const auto value = qvariant_cast<CodexTurn>(finished.front().at(0)); QCOMPARE(value.items.size(), 2);
        QCOMPARE(value.items.at(0).id, QStringLiteral("commentary")); QCOMPARE(value.items.at(0).text, QStringLiteral("reconciled"));
        QCOMPARE(value.items.at(1).text, QStringLiteral("final"));
    }
    void terminalStatuses_data()
    {
        QTest::addColumn<QString>("status"); QTest::addColumn<int>("expected");
        QTest::newRow("completed") << QStringLiteral("completed") << int(CodexTurnStatus::Completed);
        QTest::newRow("failed") << QStringLiteral("failed") << int(CodexTurnStatus::Failed);
        QTest::newRow("interrupted") << QStringLiteral("interrupted") << int(CodexTurnStatus::Interrupted);
    }
    void terminalStatuses()
    {
        QFETCH(QString, status); QFETCH(int, expected); Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.beginTurn());
        QSignalSpy finished(&s.client, &CodexClient::turnFinished); auto final = turn("turn-1", status);
        if (status == "failed") final.insert("error", QJsonObject{{"message", "secret raw error"}, {"codexErrorInfo", "unauthorized"}});
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", final}}); QCOMPARE(finished.size(), 1);
        const auto value = qvariant_cast<CodexTurn>(finished.front().at(0)); QCOMPARE(int(value.status), expected);
        if (status == "failed") { QCOMPARE(s.client.account().mode, CodexAccountMode::Expired); QVERIFY(!s.client.canSend()); QVERIFY(!value.error.userMessage.contains("secret")); }
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", final}}); QCOMPARE(finished.size(), 1);
    }
    void scopedResumeReadHistoryAndAccountChanges()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); const auto binding = s.client.threadBinding();
        auto wrong = binding; wrong.accountFingerprint = "other"; QVERIFY(!s.client.resumeThread(wrong)); wrong = binding; wrong.codexHome += "/other"; QVERIFY(!s.client.readThread(wrong));
        wrong = binding; wrong.scratchDirectory += "/other"; QVERIFY(!s.client.resumeThread(wrong)); wrong = binding; wrong.profileFingerprint = "other"; QVERIFY(!s.client.resumeThread(wrong));
        QVERIFY(s.client.resumeThread(binding)); const auto params = s.transport.lastRequest("thread/resume").value("params").toObject();
        QVERIFY(!params.contains("dynamicTools")); QVERIFY(!params.contains("environments")); QVERIFY(!params.contains("path")); QVERIFY(!params.contains("history"));
        s.reply("thread/resume", s.threadResult(s.thread("thread-1", QJsonArray{turn("old-turn", "completed", QJsonArray{agent("old-item", "stored")})})));
        QSignalSpy history(&s.client, &CodexClient::threadReceived); QVERIFY(s.client.readThread(binding)); s.reply("thread/read", {{"thread", s.thread("thread-1", QJsonArray{turn("old-turn", "completed")})}});
        QCOMPARE(history.size(), 1); QCOMPARE(qvariant_cast<CodexThread>(history.front().at(0)).turns.size(), 1);
        s.transport.event("account/updated", {{"authMode", "chatgpt"}, {"planType", "plus"}}); QVERIFY(!s.client.canSend());
        s.reply("account/read", chatGptAccount("new-account")); QVERIFY(!s.client.resumeThread(binding)); QVERIFY(!s.client.canSend());
    }
    void historyAndProfileCaps_data()
    {
        QTest::addColumn<QString>("variant"); for (const auto& name : {"paginated", "environments", "wrong thread", "too many turns", "unsafe item", "wrong cwd"}) QTest::newRow(name) << QString::fromLatin1(name);
    }
    void historyAndProfileCaps()
    {
        QFETCH(QString, variant); CodexClientLimits limits; limits.historyTurns = 1; Session s(limits); QVERIFY(s.ready()); QVERIFY(s.openThread());
        QSignalSpy failed(&s.client, &CodexClient::protocolFailed); QVERIFY(s.client.readThread(s.client.threadBinding())); auto value = s.thread();
        if (variant == "paginated") value.insert("historyMode", "paginated");
        if (variant == "environments") value.insert("environments", QJsonValue(QJsonValue::Null));
        if (variant == "wrong thread") value.insert("id", "other-thread");
        if (variant == "too many turns") value.insert("turns", QJsonArray{turn("old-1", "completed"), turn("old-2", "completed")});
        if (variant == "unsafe item") value.insert("turns", QJsonArray{turn("old-1", "completed", QJsonArray{QJsonObject{{"type", "imageView"}, {"id", "image"}, {"path", "outside"}}})});
        if (variant == "wrong cwd") value.insert("cwd", "C:/other");
        s.reply("thread/read", {{"thread", value}}); QCOMPARE(failed.size(), 1); QVERIFY(!s.client.canSend());
    }
    void loginFlows_data() { QTest::addColumn<bool>("device"); QTest::newRow("browser") << false; QTest::newRow("device code") << true; }
    void loginFlows()
    {
        QFETCH(bool, device); Session s; QVERIFY(s.ready({{"account", QJsonValue(QJsonValue::Null)}, {"requiresOpenaiAuth", true}}));
        QSignalSpy challenges(&s.client, &CodexClient::loginChallenge), finished(&s.client, &CodexClient::loginFinished), cleared(&s.client, &CodexClient::loginCleared);
        QVERIFY(s.client.startLogin(device ? CodexLoginMode::DeviceCode : CodexLoginMode::Browser));
        const auto expected = device ? QStringLiteral("chatgptDeviceCode") : QStringLiteral("chatgpt");
        QCOMPARE(s.transport.lastRequest("account/login/start").value("params").toObject(), (QJsonObject{{"type", expected}}));
        s.reply("account/login/start", device ? QJsonObject{{"type", expected}, {"loginId", "login-1"}, {"verificationUrl", "https://auth.openai.com/codex/device"}, {"userCode", "FAKE-1234"}}
            : QJsonObject{{"type", expected}, {"loginId", "login-1"}, {"authUrl", "https://auth.openai.com/oauth/authorize?state=fixture"}});
        QCOMPARE(challenges.size(), 1); QVERIFY(!s.client.startLogin(CodexLoginMode::Browser));
        s.transport.event("account/login/completed", {{"loginId", "unrelated"}, {"success", true}}); QCOMPARE(finished.size(), 0);
        s.transport.event("account/login/completed", {{"loginId", "login-1"}, {"success", true}}); QCOMPARE(finished.size(), 1); QCOMPARE(cleared.size(), 1);
        s.reply("account/read", chatGptAccount()); QCOMPARE(s.client.account().mode, CodexAccountMode::ChatGpt);
        s.transport.event("account/login/completed", {{"loginId", "login-1"}, {"success", true}}); QCOMPARE(finished.size(), 1);
    }
    void earlyLoginCompletionAndCancellation()
    {
        Session s; QVERIFY(s.ready()); QSignalSpy challenge(&s.client, &CodexClient::loginChallenge), finished(&s.client, &CodexClient::loginFinished);
        QVERIFY(s.client.startLogin(CodexLoginMode::Browser)); s.transport.event("account/login/completed", {{"loginId", "login-1"}, {"success", true}});
        s.reply("account/login/start", {{"type", "chatgpt"}, {"loginId", "login-1"}, {"authUrl", "https://auth.openai.com/oauth/authorize?state=fixture"}});
        QCOMPARE(challenge.size(), 0); QCOMPARE(finished.size(), 1); s.reply("account/read", chatGptAccount());
        QVERIFY(s.client.startLogin(CodexLoginMode::DeviceCode)); s.reply("account/login/start", {{"type", "chatgptDeviceCode"}, {"loginId", "login-2"}, {"verificationUrl", "https://auth.openai.com/codex/device"}, {"userCode", "FAKE"}});
        QVERIFY(s.client.cancelLogin()); QCOMPARE(s.transport.lastRequest("account/login/cancel").value("params").toObject(), (QJsonObject{{"loginId", "login-2"}}));
        s.transport.event("account/login/completed", {{"loginId", "login-2"}, {"success", true}}); QCOMPARE(finished.size(), 2);
        s.reply("account/login/cancel", {{"status", "canceled"}}); QVERIFY(!s.client.cancelLogin());
    }
    void loginChallengeExpiryAndHostValidation()
    {
        CodexClientLimits limits; limits.loginLifetimeMilliseconds = 30; Session s(limits); QVERIFY(s.ready());
        QSignalSpy cleared(&s.client, &CodexClient::loginCleared), finished(&s.client, &CodexClient::loginFinished);
        QVERIFY(s.client.startLogin(CodexLoginMode::DeviceCode)); s.reply("account/login/start", {{"type", "chatgptDeviceCode"}, {"loginId", "expired"}, {"verificationUrl", "https://auth.openai.com/codex/device"}, {"userCode", "FAKE"}});
        QTRY_COMPARE_WITH_TIMEOUT(cleared.size(), 1, 1000); QCOMPARE(finished.size(), 1); QCOMPARE(s.transport.lastRequest("account/login/cancel").value("params").toObject().value("loginId"), QJsonValue("expired"));
        s.reply("account/login/cancel", {{"status", "notFound"}});
        for (const auto& url : {"http://auth.openai.com/oauth/authorize", "https://auth.openai.com.evil.test/oauth/authorize", "https://user:secret@auth.openai.com/oauth/authorize", "https://auth.openai.com:444/oauth/authorize", "file:///oauth/authorize", "https://chatgpt.com/unsupported"}) QVERIFY(!CodexRuntimeProfile::acceptsLoginUrl(QUrl(url), false));
        QVERIFY(s.client.startLogin(CodexLoginMode::Browser)); QSignalSpy failed(&s.client, &CodexClient::protocolFailed);
        s.reply("account/login/start", {{"type", "chatgpt"}, {"loginId", "login-bad"}, {"authUrl", "https://evil.test/oauth/authorize"}}); QCOMPARE(failed.size(), 1);
    }
    void modelsAreAvailabilityNotEntitlement()
    {
        Session s; QVERIFY(s.ready()); QSignalSpy received(&s.client, &CodexClient::modelsReceived); QVERIFY(s.client.listModels());
        const QJsonObject model{{"id", "fixture"}, {"model", "fixture-model"}, {"displayName", "Fixture"}, {"description", "Synthetic model"},
            {"defaultReasoningEffort", "medium"}, {"supportedReasoningEfforts", QJsonArray{}}, {"hidden", false}, {"isDefault", true},
            {"inputModalities", QJsonArray{"text"}}, {"availabilityNux", QJsonObject{{"message", "Not available to this account"}}}};
        s.reply("model/list", {{"data", QJsonArray{model}}, {"nextCursor", "page-2"}}); QCOMPARE(received.size(), 1);
        const auto page = qvariant_cast<CodexModelPage>(received.front().at(0)); QCOMPARE(page.nextCursor, QStringLiteral("page-2")); QCOMPARE(page.models.size(), 1);
        QVERIFY(!page.models.front().entitlementVerified); QVERIFY(page.models.front().supportsText); QVERIFY(!page.models.front().availabilityMessage.isEmpty());
        QVERIFY(s.client.listModels(page.nextCursor)); QCOMPARE(s.transport.lastRequest("model/list").value("params").toObject().value("cursor"), QJsonValue("page-2"));
        s.reply("model/list", {{"data", QJsonArray{}}}); QCOMPARE(received.size(), 2);
    }
    void deadlinesLateRepliesAndNoReplay()
    {
        CodexClientLimits limits; limits.acknowledgementMilliseconds = 250; Session s(limits); QVERIFY(s.ready()); QVERIFY(s.openThread());
        QSignalSpy failed(&s.client, &CodexClient::requestFailed), accepted(&s.client, &CodexClient::turnAccepted);
        const auto request = s.client.startTurn("once"); QVERIFY(request);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 1000); QCOMPARE(qvariant_cast<devhub::domain::errors::AppError>(failed.front().at(2)).code, ErrorCode::CodexRequestTimeout);
        const auto count = s.transport.sent.size(); s.transport.reply(request.value(), {{"turn", turn()}}); QCOMPARE(accepted.size(), 0); QCOMPARE(s.transport.sent.size(), count);
        QVERIFY(!s.client.startTurn("replay")); QVERIFY(!s.client.startThread("new uncertain thread")); QCOMPARE(failed.size(), 1);
    }
    void processGenerationLossAndRestart()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.beginTurn()); QSignalSpy failed(&s.client, &CodexClient::requestFailed), finished(&s.client, &CodexClient::turnFinished);
        QVERIFY(s.client.listModels()); const auto oldId = s.requestId("model/list"); emit s.transport.failed(devhub::domain::errors::AppError{}, 1);
        QCOMPARE(failed.size(), 1); QCOMPARE(finished.size(), 1); QCOMPARE(qvariant_cast<CodexTurn>(finished.front().at(0)).status, CodexTurnStatus::Uncertain); QCOMPARE(s.client.pendingRequestCount(), 0);
        QVERIFY(!s.client.launch(s.spec(1))); QVERIFY(s.client.launch(s.spec(2))); const auto currentId = s.requestId("initialize"); QVERIFY(currentId > oldId);
        emit s.transport.objectReceived({{"id", currentId}, {"result", QJsonObject{}}}, 1); QVERIFY(!s.client.isInitialized()); QCOMPARE(s.client.pendingRequestCount(), 1);
        s.transport.reply(oldId, {{"data", QJsonArray{}}}); QCOMPARE(s.client.pendingRequestCount(), 1); QCOMPARE(failed.size(), 1);
    }
    void staleAccountResponseCannotRestoreReadiness()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.client.readAccount(true)); const auto oldId = s.requestId("account/read");
        s.transport.event("account/updated", {{"authMode", QJsonValue(QJsonValue::Null)}}); const auto newId = s.requestId("account/read"); QVERIFY(newId > oldId);
        s.transport.reply(oldId, chatGptAccount()); QVERIFY(!s.client.canSend()); QCOMPARE(s.client.account().mode, CodexAccountMode::SignedOut);
        s.transport.reply(newId, {{"account", QJsonValue(QJsonValue::Null)}, {"requiresOpenaiAuth", true}}); QVERIFY(!s.client.canSend());
    }
    void eventCapsAndDisabledItemCannotContinue_data()
    {
        QTest::addColumn<bool>("early"); QTest::newRow("early event cap") << true; QTest::newRow("disabled item") << false;
    }
    void eventCapsAndDisabledItemCannotContinue()
    {
        QFETCH(bool, early); CodexClientLimits limits; limits.earlyEvents = 1; Session s(limits); QVERIFY(s.ready()); QVERIFY(s.openThread());
        QSignalSpy failed(&s.client, &CodexClient::protocolFailed); if (early) { QVERIFY(s.client.startTurn("once")); s.delta("one"); s.delta("two"); }
        else { QVERIFY(s.beginTurn()); s.item("item/started", {{"type", "imageView"}, {"id", "image"}, {"path", "outside"}}); }
        QCOMPARE(failed.size(), 1); QVERIFY(!s.client.canSend()); QCOMPARE(s.transport.stops, 1);
    }
    void synchronousFakeResponseAndDestruction()
    {
        Session s; QVERIFY(s.ready()); QSignalSpy models(&s.client, &CodexClient::modelsReceived);
        s.transport.onSend = [&](const QJsonObject& object) { if (object.value("method") == "model/list") s.transport.reply(object.value("id").toInteger(), {{"data", QJsonArray{}}}); };
        QVERIFY(s.client.listModels()); QCOMPARE(models.size(), 1); QCOMPARE(s.client.pendingRequestCount(), 0);
        auto* transport = new MemoryCodexTransport; auto* client = new CodexClient(*transport); QSignalSpy failed(client, &CodexClient::requestFailed);
        QVERIFY(client->launch(s.spec())); delete transport; QCOMPARE(failed.size(), 1); QVERIFY(!client->isInitialized()); delete client;
    }
    void rejectedAccountCheckRevokesSend()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); const auto pending = s.client.readAccount(true); QVERIFY(pending); QVERIFY(!s.client.canSend());
        s.transport.deliver({{"id", pending.value()}, {"error", QJsonObject{{"code", -32603}, {"message", "private details"}}}});
        QVERIFY(!s.client.canSend()); QCOMPARE(s.client.account().mode, CodexAccountMode::Unknown);
    }
    void accountReadIdentityChangeInvalidatesInFlightThread()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.client.startThread("fixture-model")); QVERIFY(s.client.readAccount());
        s.reply("account/read", chatGptAccount("other-account")); QSignalSpy failed(&s.client, &CodexClient::protocolFailed);
        s.reply("thread/start", s.threadResult()); QCOMPARE(failed.size(), 1); QVERIFY(s.client.threadBinding().threadId.isEmpty());
    }
    void accountChangeDuringActiveTurn_data()
    { QTest::addColumn<bool>("notification"); QTest::newRow("account update") << true; QTest::newRow("different account read") << false; }
    void accountChangeDuringActiveTurn()
    {
        QFETCH(bool, notification); Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.beginTurn());
        QSignalSpy finished(&s.client, &CodexClient::turnFinished); QSignalSpy changed(&s.client, &CodexClient::itemChanged);
        if (notification) s.transport.event("account/updated", {{"authMode", "chatgpt"}});
        else { QVERIFY(s.client.readAccount()); s.reply("account/read", chatGptAccount("other-account")); }
        QCOMPARE(finished.size(), 1); QCOMPARE(qvariant_cast<CodexTurn>(finished.front().at(0)).status, CodexTurnStatus::Uncertain);
        QCOMPARE(s.transport.stops, 1); QVERIFY(!s.client.isInitialized()); QVERIFY(!s.client.canSend());
        s.delta("stale account text"); QCOMPARE(changed.size(), 0); QCOMPARE(finished.size(), 1);
    }
    void unknownNotificationsAndEarlyThreadEventsHaveNoOwnership()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.client.startThread("fixture-model"));
        s.transport.event("thread/started", {{"thread", s.thread("foreign-thread")}});
        s.transport.event("future/unknown", {{"payload", "ignored"}}); QVERIFY(s.client.threadBinding().threadId.isEmpty());
        s.reply("thread/start", s.threadResult()); QCOMPARE(s.client.threadBinding().threadId, QStringLiteral("thread-1"));
        QCOMPARE(s.client.ignoredMessageCount(), quint64(2));
    }
    void completionCallbackCanStartAnotherTurn()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.client.startTurn("first"));
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", turn("turn-1", "completed")}});
        bool started = false;
        connect(&s.client, &CodexClient::turnFinished, &s.client, [&](const CodexTurn&) {
            started = bool(s.client.startTurn("second")); s.reply("turn/start", {{"turn", turn("turn-2")}});
        });
        // The first acknowledgement contains stale items after its early
        // completion. They must not enter the newly started turn's item map.
        s.reply("turn/start", {{"turn", turn("turn-1", "inProgress", QJsonArray{agent("old-item", "old text")})}});
        QVERIFY(started); QCOMPARE(s.client.activeTurnId(), QStringLiteral("turn-2")); QSignalSpy changed(&s.client, &CodexClient::itemChanged);
        s.delta("new text", "old-item", "turn-2"); QCOMPARE(changed.size(), 1);
        QCOMPARE(qvariant_cast<CodexItem>(changed.front().at(0)).text, QStringLiteral("new text"));
    }
    void writeRejectionAndServerRequestCap()
    {
        CodexClientLimits limits; limits.serverRequests = 1; Session s(limits); QVERIFY(s.ready()); QVERIFY(s.openThread());
        s.transport.rejectWrites = true; QSignalSpy failed(&s.client, &CodexClient::requestFailed);
        QVERIFY(!s.client.startTurn("not queued")); QCOMPARE(failed.size(), 1); QCOMPARE(s.client.pendingRequestCount(), 0); QVERIFY(s.client.canSend());
        s.transport.rejectWrites = false; s.transport.deliver({{"id", "one"}, {"method", "future"}});
        QSignalSpy protocol(&s.client, &CodexClient::protocolFailed); s.transport.deliver({{"id", "two"}, {"method", "future"}}); QCOMPARE(protocol.size(), 1);
    }
    void acknowledgementTimeouts_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::newRow("initialize") << QStringLiteral("initialize");
        QTest::newRow("login") << QStringLiteral("account/login/start");
        QTest::newRow("interrupt") << QStringLiteral("turn/interrupt");
    }
    void acknowledgementTimeouts()
    {
        QFETCH(QString, operation); CodexClientLimits limits; limits.acknowledgementMilliseconds = 250; limits.loginAcknowledgementMilliseconds = 250; Session s(limits);
        QSignalSpy failed(&s.client, &CodexClient::requestFailed);
        if (operation == "initialize") QVERIFY(s.client.launch(s.spec()));
        else {
            QVERIFY(s.ready());
            if (operation == "account/login/start") QVERIFY(s.client.startLogin(CodexLoginMode::Browser));
            else { QVERIFY(s.openThread()); QVERIFY(s.beginTurn()); QVERIFY(s.client.interruptTurn()); }
        }
        QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), 1000); QCOMPARE(failed.front().at(1).toString(), operation);
        QCOMPARE(qvariant_cast<devhub::domain::errors::AppError>(failed.front().at(2)).code, ErrorCode::CodexRequestTimeout);
        if (operation == "turn/interrupt") { QCOMPARE(s.client.activeTurnId(), QStringLiteral("turn-1")); QVERIFY(!s.client.canSend()); }
        else { QVERIFY(!s.client.isInitialized()); QCOMPARE(s.transport.stops, 1); }
    }
    void expiredResponseBeforeTimerDispatch_data()
    { QTest::addColumn<bool>("login"); QTest::newRow("initialize") << false; QTest::newRow("login") << true; }
    void expiredResponseBeforeTimerDispatch()
    {
        QFETCH(bool, login); CodexClientLimits limits; limits.acknowledgementMilliseconds = 250; limits.loginAcknowledgementMilliseconds = 250; Session s(limits);
        if (login) { QVERIFY(s.ready()); QVERIFY(s.client.startLogin(CodexLoginMode::Browser)); }
        else QVERIFY(s.client.launch(s.spec()));
        QSignalSpy failed(&s.client, &CodexClient::requestFailed); QSignalSpy challenge(&s.client, &CodexClient::loginChallenge);
        // Block event dispatch deliberately: a response must enforce its own
        // expired deadline even when the timer has not run yet.
        QTest::qSleep(300);
        if (login) s.reply("account/login/start", {{"type", "chatgpt"}, {"loginId", "late"}, {"authUrl", "https://auth.openai.com/oauth/authorize"}});
        else s.reply("initialize", {{"codexHome", s.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        QCOMPARE(failed.size(), 1); QCOMPARE(challenge.size(), 0); QCOMPARE(s.transport.stops, 1); QVERIFY(!s.client.isInitialized());
        QCOMPARE(qvariant_cast<devhub::domain::errors::AppError>(failed.front().at(2)).code, ErrorCode::CodexRequestTimeout);
    }
    void earlyBytesAndStreamingTextCaps_data()
    { QTest::addColumn<bool>("early"); QTest::newRow("early byte budget") << true; QTest::newRow("streamed item budget") << false; }
    void earlyBytesAndStreamingTextCaps()
    {
        QFETCH(bool, early); CodexClientLimits limits; limits.earlyEventBytes = 1; limits.itemTextBytes = 10; Session s(limits); QVERIFY(s.ready()); QVERIFY(s.openThread());
        QVERIFY(s.client.startTurn("hello")); if (!early) s.reply("turn/start", {{"turn", turn()}});
        QSignalSpy failed(&s.client, &CodexClient::protocolFailed); s.delta("more than ten bytes"); QCOMPARE(failed.size(), 1); QVERIFY(!s.client.canSend());
    }
    void terminalResponseOnlyAndFailedAckWithEarlyEvents()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QSignalSpy finished(&s.client, &CodexClient::turnFinished);
        QVERIFY(s.client.startTurn("terminal in acknowledgement")); s.reply("turn/start", {{"turn", turn("turn-1", "completed", QJsonArray{agent("only", "final text")})}});
        QCOMPARE(finished.size(), 1); QCOMPARE(qvariant_cast<CodexTurn>(finished.front().at(0)).items.front().text, QStringLiteral("final text"));
        QVERIFY(s.client.startTurn("uncertain")); s.delta("observed execution", "agent-2", "turn-2");
        s.transport.deliver({{"id", s.requestId("turn/start")}, {"error", QJsonObject{{"code", -32603}, {"message", "failure after an event"}}}});
        QVERIFY(!s.client.canSend()); QVERIFY(!s.client.startTurn("unsafe retry"));
    }
    void environmentAllowlistAndProfileIdentity()
    {
        QProcessEnvironment environment; environment.insert("OPENAI_API_KEY", "synthetic-secret"); QVERIFY(!CodexRuntimeProfile::acceptsEnvironment(environment));
        environment.clear(); environment.insert("CODEX_HOME", "relative"); QVERIFY(!CodexRuntimeProfile::acceptsEnvironment(environment));
        Session s; environment.insert("CODEX_HOME", s.scratch.path()); QVERIFY(CodexRuntimeProfile::acceptsEnvironment(environment));
        const auto other = CodexRuntimeProfile::launchArguments({"disabled-server"}); QVERIFY(other);
        QVERIFY(CodexRuntimeProfile::fingerprint() != CodexRuntimeProfile::fingerprint(other.value()));
    }
    void timeoutCallbackCanReconnectWithoutKillingNewGeneration()
    {
        CodexClientLimits limits; limits.acknowledgementMilliseconds = 250; Session s(limits);
        bool restarted = false;
        connect(&s.client, &CodexClient::requestFailed, &s.client, [&](qint64, const QString&, const devhub::domain::errors::AppError&) {
            if (restarted) return;
            s.client.stop(); restarted = bool(s.client.launch(s.spec(2)));
            // Acknowledge the replacement inline so its deliberately short
            // deadline cannot also expire during QTRY's event-loop wait.
            s.reply("initialize", {{"codexHome", s.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
            s.reply("account/read", chatGptAccount()); s.reply("config/read", safeConfig());
            s.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        });
        QVERIFY(s.client.launch(s.spec())); QTRY_VERIFY_WITH_TIMEOUT(restarted, 1000);
        QCOMPARE(s.transport.generation, quint64(2)); QCOMPARE(s.transport.stops, 1); QCOMPARE(s.client.pendingRequestCount(), 0);
        QVERIFY(s.client.isInitialized()); QVERIFY(s.client.profileVerified());
    }
    void retryErrorNotificationDoesNotFinalizeTurn()
    {
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.beginTurn()); QSignalSpy finished(&s.client, &CodexClient::turnFinished);
        // Phase 0 did not freeze this optional notification's shape. It remains
        // an unknown notification; terminal state comes from the typed turn.
        s.transport.event("error", {{"threadId", "thread-1"}, {"turnId", "turn-1"}, {"willRetry", true}});
        QCOMPARE(finished.size(), 0); QCOMPARE(s.client.activeTurnId(), QStringLiteral("turn-1"));
    }
};
QTEST_GUILESS_MAIN(AssistantClientTest)
#include "assistant_client_test.moc"
