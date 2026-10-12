#include "infrastructure/codex/CodexClient.h"
#include "infrastructure/codex/CodexRuntimeProfile.h"
#include "infrastructure/codex/CodexProcess.h"
#include "infrastructure/codex/CodexVersionSupport.h"
#include "application/assistant/AssistantToolRuntime.h"
#include "support/MemoryCodexTransport.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace devhub::application::assistant;
using namespace devhub::infrastructure::codex;
using devhub::domain::errors::ErrorCode;
#include "support/CodexTestSession.h"
using namespace devhub::tests::codex;

namespace {
class ScopedEnvironmentVariable final {
public:
    explicit ScopedEnvironmentVariable(QByteArray name)
        : m_name(std::move(name)), m_wasSet(qEnvironmentVariableIsSet(m_name.constData())),
          m_previous(qgetenv(m_name.constData()))
    {
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_wasSet) qputenv(m_name.constData(), m_previous);
        else qunsetenv(m_name.constData());
    }

    void set(const QByteArray& value) { qputenv(m_name.constData(), value); }

private:
    QByteArray m_name;
    bool m_wasSet;
    QByteArray m_previous;
};
} // namespace

class AssistantClientTest final : public QObject {
    Q_OBJECT
private slots:
    void nativeAlphaOfflineInheritedProfile() {
        const auto executable = qEnvironmentVariable("DEVHUB_OFFLINE_CODEX_EXE");
        if (executable.isEmpty()) QSKIP("Explicit native executable required; no inference or authentication.");
        QTemporaryDir home; QTemporaryDir scratch; QVERIFY(home.isValid() && scratch.isValid());
        QFile config(home.filePath("config.toml")); QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("notify = [\"nonexistent-notification-command\"]\n"
            "[mcp_servers.inherited]\ncommand = \"nonexistent-mcp-command\"\nenabled = true\n"
            "[desktop]\nambient-suggestions-enabled = true\nsansFontSize = 13\ncodeFontSize = 12\nconversationDetailMode = \"normal\"\nfollowUpQueueMode = \"queue\"\n");
        config.write(QStringLiteral("[mcp_servers.\"name.with.dots\"]\ncommand=\"nonexistent-mcp-command\"\n"
            "[mcp_servers.\"quote\\\"name\"]\ncommand=\"nonexistent-mcp-command\"\n"
            "[mcp_servers.\"native İ\"]\ncommand=\"nonexistent-mcp-command\"\n").toUtf8()); config.close();
        auto environment = codexBaseEnvironment(QProcessEnvironment::systemEnvironment()); environment.insert("CODEX_HOME", home.path());
        CodexProcess process; CodexClient client(process);
        QVERIFY(client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications()));
        QSignalSpy checked(&client, &ICodexClient::profileChecked), retry(&client, &ICodexClient::restrictedLaunchRequired), stopped(&client, &ICodexClient::backendStopped);
        QVERIFY(client.launch({{executable, "0.162.0-alpha.17.2"}, CodexRuntimeProfile::launchArguments().value(), scratch.path(), environment, 1}));
        QTRY_COMPARE_WITH_TIMEOUT(retry.size(), 1, 15000); QVERIFY(!client.profileVerified());
        QCOMPARE(checked.first().at(1).toStringList(), QStringList{"inherited-mcp-enabled"});
        const auto arguments = retry.first().first().toStringList(); QVERIFY(CodexRuntimeProfile::acceptsLaunch(arguments));
        client.stop(); QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 10000); checked.clear();
        QVERIFY(client.launch({{executable, "0.162.0-alpha.17.2"}, arguments, scratch.path(), environment, 2}));
        QTRY_VERIFY_WITH_TIMEOUT(!checked.isEmpty(), 15000);
        QVERIFY2(client.profileVerified(), qPrintable(checked.first().at(1).toStringList().join(',')));
        QVERIFY(!client.canStartConversation()); // Disposable profile is signed out.
        client.stop(); QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 2, 10000);
        QVERIFY(config.open(QIODevice::ReadOnly));
        const auto original = config.readAll(); QVERIFY(original.contains("ambient-suggestions-enabled = true"));
        QVERIFY(original.contains("enabled = true")); QVERIFY(original.contains("nonexistent-notification-command"));
    }
    void nativeAlphaOfflineProfile()
    {
        const QString executable = qEnvironmentVariable("DEVHUB_OFFLINE_CODEX_EXE");
        if (executable.isEmpty()) QSKIP("Explicit native executable required for isolated offline profile check.");
        QTemporaryDir home; QTemporaryDir scratch;
        QVERIFY(home.isValid()); QVERIFY(scratch.isValid());
        auto environment = codexBaseEnvironment(QProcessEnvironment::systemEnvironment());
        environment.insert(QStringLiteral("CODEX_HOME"), home.path());
        QProcess versionProbe; versionProbe.setProgram(executable); versionProbe.setArguments({"--version"});
        versionProbe.setProcessEnvironment(environment); versionProbe.start();
        QVERIFY(versionProbe.waitForStarted(3000)); QVERIFY(versionProbe.waitForFinished(5000));
        QCOMPARE(versionProbe.exitCode(), 0);
        QCOMPARE(CodexVersionSupport::parseVersionResponse(QString::fromUtf8(versionProbe.readAllStandardOutput())).value_or(QString{}),
            QStringLiteral("0.162.0-alpha.17.2"));
        CodexProcess process; CodexClient client(process);
        connect(&process, &ICodexTransport::objectReceived, &client, [](const QJsonObject& message, quint64) {
            const auto result = message.value("result").toObject();
            if (result.contains("config")) {
                QStringList sources;
                for (const auto& layer : result.value("layers").toArray())
                    sources.append(layer.toObject().value("name").toObject().value("type").toString());
                qInfo().noquote() << "Offline config source types:" << sources.join(',');
            }
            if (result.value("requirements").isObject()) {
                const auto requirements = result.value("requirements").toObject();
                for (auto it = requirements.begin(); it != requirements.end(); ++it)
                    if (!it.value().isNull()) qInfo().noquote() << "Offline non-null requirement:" << it.key();
            }
        });
        QVERIFY(client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications()));
        QSignalSpy checked(&client, &ICodexClient::profileChecked);
        QSignalSpy stopped(&client, &ICodexClient::backendStopped);
        QVERIFY(client.launch({{executable, QStringLiteral("0.162.0-alpha.17.2")},
            CodexRuntimeProfile::launchArguments().value(), scratch.path(), environment, 1}));
        QTRY_VERIFY_WITH_TIMEOUT(!checked.isEmpty(), 15000);
        const auto reasons = checked.first().at(1).toStringList();
        QVERIFY2(client.profileVerified(), qPrintable(reasons.join(',')));
        QVERIFY(!client.canStartConversation()); // no auth or inference in this test
        QVERIFY(!client.startThread(QStringLiteral("fixture-model")));
        client.stop(); QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 10000);
    }

    void handshakeAndRequiredFields()
    {
        Session s; QSignalSpy initialized(&s.client, &CodexClient::initialized);
        QVERIFY(s.client.launch(s.spec()));
        const auto initialize = s.transport.lastRequest("initialize");
        QCOMPARE(initialize.value("params").toObject().value("clientInfo").toObject(),
            (QJsonObject{{"name", "devhub"}, {"title", "DevHub"}, {"version", DEVHUB_VERSION}}));
        QVERIFY(!initialize.value("params").toObject().value("capabilities").toObject().value("experimentalApi").toBool());
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
    void alphaContractsAndVersionScopedConversationBinding()
    {
        constexpr auto alpha = "0.162.0-alpha.17.2";
        Session s;
        QVERIFY(s.client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications()));
        QVERIFY(s.client.launch(s.spec(1, CodexRuntimeProfile::launchArguments().value(), QString::fromLatin1(alpha))));
        const auto initialize = s.transport.lastRequest("initialize");
        QVERIFY(!initialize.isEmpty());
        QVERIFY(initialize.value(QStringLiteral("params")).toObject().value(QStringLiteral("capabilities")).toObject()
            .value(QStringLiteral("experimentalApi")).toBool());
        s.reply("initialize", {{"codexHome", s.scratch.path()}, {"userAgent", "fixture"},
            {"platformFamily", "windows"}, {"platformOs", "windows"}});
        s.reply("account/read", chatGptAccount());
        s.reply("config/read", safeConfig());
        s.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)},
            {"supportsIndependentSpeedModes", true}});
        QVERIFY(s.client.profileVerified());
        const CodexThreadBinding previousVersionBinding{"saved-thread", s.client.account().identityFingerprint,
            s.scratch.path(), {}, s.scratch.path(), CodexRuntimeProfile::fingerprint(QStringList{})};
        QVERIFY(!s.client.resumeThread(previousVersionBinding));
        QVERIFY(s.client.startThread(QStringLiteral("fixture-model")));
        const auto start = s.transport.lastRequest("thread/start");
        QVERIFY(!start.isEmpty());
        s.reply("thread/start", s.threadResult());
        QVERIFY(s.client.threadBinding().profileFingerprint !=
            CodexRuntimeProfile::fingerprint(QString::fromLatin1(alpha), CodexRuntimeProfile::launchArguments().value()));
        QVERIFY(s.client.threadBinding().profileFingerprint != previousVersionBinding.profileFingerprint);
        QCOMPARE(s.transport.lastRequest(QStringLiteral("thread/start")).value(QStringLiteral("params")).toObject()
            .value(QStringLiteral("dynamicTools")).toArray(), AssistantToolRuntime::experimentalToolSpecifications());
        QVERIFY(s.client.startTurn(QStringLiteral("alpha contract fixture")));
        QVERIFY(s.transport.lastRequest("turn/start").contains("params"));
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
        QVERIFY(!CodexRuntimeProfile::productionChatEnabled("0.160.1"));
    }
    void productionBuildCannotEnableLiveValidationFromEnvironmentOrSettings()
    {
        ScopedEnvironmentVariable inferenceOptIn("DEVHUB_RUN_LIVE_ASSISTANT_TESTS");
        ScopedEnvironmentVariable executablePath("DEVHUB_LIVE_CODEX_EXE");
        ScopedEnvironmentVariable disposableHome("DEVHUB_LIVE_CODEX_HOME");
        ScopedEnvironmentVariable disposableAck("DEVHUB_LIVE_DISPOSABLE_PROFILE_ACK");
        inferenceOptIn.set("I_ACKNOWLEDGE_REAL_CODEX_INFERENCE");
        executablePath.set("C:/fixture/codex.exe");
        disposableHome.set("C:/fixture/disposable-codex-home");
        disposableAck.set("I_CREATED_THIS_DISPOSABLE_PROFILE");

        QVERIFY(!CodexRuntimeProfile::developmentLiveValidationBuild());
        QVERIFY(!CodexRuntimeProfile::productionChatEnabled("0.160.1"));
        QVERIFY(!CodexRuntimeProfile::developmentLiveInferenceAuthorized(
            QStringLiteral("C:/fixture/codex.exe"), QStringLiteral("0.160.1"), QProcessEnvironment{}));

        // App settings have no route to the compile-time policy. Even after a
        // valid account, capability profile and thread, native admission stays
        // disabled in the production library.
        Session s; QVERIFY(s.ready()); QVERIFY(s.openThread());
        s.transport.native = true;
        QVERIFY(!s.client.canStartConversation());
        QVERIFY(!s.client.canSend());
        QVERIFY(!s.client.startTurn(QStringLiteral("synthetic test prompt")));
    }
    void admissionRejectsUnsafeProfiles_data()
    {
        QTest::addColumn<QString>("variant");
        for (const auto& name : {"shell", "mcp", "notify", "managed", "unknown layer", "missing layers", "attached", "provider", "missing features"}) QTest::newRow(name) << QString::fromLatin1(name);
    }
    void nativeProductionAdmission_data() {
        QTest::addColumn<QString>("version"); QTest::addColumn<bool>("enabled");
        QTest::newRow("legacy-inspection-only") << QString("0.160.1") << false;
        QTest::newRow("requested-alpha") << QString("0.162.0-alpha.17.2") << true;
    }
    void nativeProductionAdmission() {
        QFETCH(QString, version); QFETCH(bool, enabled);
        Session s; s.transport.native = true;
        QVERIFY(s.client.launch(s.spec(1, CodexRuntimeProfile::launchArguments().value(), version)));
        s.reply("initialize", {{"codexHome", s.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        s.reply("account/read", chatGptAccount()); s.reply("config/read", safeConfig());
        QVERIFY(!s.client.canStartConversation()); // Both profile responses required.
        s.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        QCOMPARE(CodexRuntimeProfile::productionChatEnabled(version), enabled);
        QCOMPARE(s.client.canStartConversation(), enabled); QVERIFY(!s.client.canSend());
        QCOMPARE(bool(s.client.startThread("fixture-model")), enabled);
        if (!enabled) { QVERIFY(!s.client.startTurn("blocked")); return; }
        s.reply("thread/start", s.threadResult()); QVERIFY(s.client.canSend());
        QVERIFY(s.beginTurn()); QVERIFY(!s.client.canSend());
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", turn("turn-1", "completed")}});
        QVERIFY(s.client.canSend());
        s.transport.event("account/updated", {{"authMode", QJsonValue(QJsonValue::Null)}});
        QVERIFY(!s.client.canSend()); QVERIFY(!s.client.canStartConversation());
    }
    void desktopPreferencesDoNotAttachCapabilities_data() {
        QTest::addColumn<QJsonObject>("desktop"); QTest::addColumn<bool>("accepted");
        QTest::newRow("known-appearance") << QJsonObject{{"ambient-suggestions-enabled", false}, {"sansFontSize", 13}, {"codeFontSize", 12}, {"conversationDetailMode", "normal"}, {"followUpQueueMode", "queue"}} << true;
        QTest::newRow("ambient-enabled") << QJsonObject{{"ambient-suggestions-enabled", true}} << false;
        QTest::newRow("unknown-field") << QJsonObject{{"attached", true}} << false;
        QTest::newRow("bad-font") << QJsonObject{{"sansFontSize", "13"}} << false;
        QTest::newRow("oversized-font") << QJsonObject{{"sansFontSize", 10000}} << false;
    }
    void desktopPreferencesDoNotAttachCapabilities() {
        QFETCH(QJsonObject, desktop); QFETCH(bool, accepted);
        auto result = safeConfig(); auto config = result.value("config").toObject(); config.insert("desktop", desktop); result.insert("config", config);
        QCOMPARE(CodexRuntimeProfile::rejectionReasons(result, {}).isEmpty(), accepted);
        config.insert("computer_use", QJsonObject{{"attached", true}}); result.insert("config", config);
        QVERIFY(!CodexRuntimeProfile::rejectionReasons(result, {}).isEmpty());
    }
    void inheritedMcpRequestsRestrictedRelaunch_data() {
        QTest::addColumn<QString>("variant"); QTest::addColumn<bool>("retry");
        QTest::newRow("valid") << QString("valid") << true;
        QTest::newRow("unsafe-policy") << QString("unsafe") << false;
        QTest::newRow("invalid-name") << QString("invalid") << false;
        QTest::newRow("too-many") << QString("too-many") << false;
    }
    void inheritedMcpRequestsRestrictedRelaunch() {
        QFETCH(QString, variant); QFETCH(bool, retry); Session s; QVERIFY(s.start());
        QSignalSpy adjustment(&s.client, &ICodexClient::restrictedLaunchRequired);
        auto result = safeConfig(); auto config = result.value("config").toObject();
        QJsonObject servers{{"name.with.dots", QJsonObject{{"enabled", true}}}};
        if (variant == "unsafe") config.insert("notify", QJsonArray{"private-command"});
        if (variant == "invalid") servers.insert("bad\nname", QJsonObject{{"enabled", true}});
        if (variant == "too-many") for (int i = 0; i < 65; ++i) servers.insert(QString::number(i), QJsonObject{});
        config.insert("mcp_servers", servers); result.insert("config", config);
        s.reply("config/read", result); s.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        QVERIFY(!s.client.profileVerified()); QVERIFY(!s.client.canStartConversation());
        QCOMPARE(adjustment.size(), retry ? 1 : 0);
        if (retry) {
            const auto arguments = adjustment.first().first().toStringList();
            QVERIFY(CodexRuntimeProfile::acceptsLaunch(arguments));
            QVERIFY(arguments.contains("mcp_servers={\"name.with.dots\"={enabled=false}}"));
            QCOMPARE(adjustment.first().at(1).toULongLong(), quint64(1));
        }
    }
    void localSystemLayerAndLoginConstraints_data()
    {
        QTest::addColumn<QJsonValue>("methods"); QTest::addColumn<bool>("accepted");
        QTest::newRow("chatgpt-only") << QJsonValue(QJsonArray{"chatgpt"}) << true;
        QTest::newRow("chatgpt-and-api") << QJsonValue(QJsonArray{"chatgpt", "api"}) << true;
        QTest::newRow("api-only") << QJsonValue(QJsonArray{"api"}) << false;
        QTest::newRow("empty") << QJsonValue(QJsonArray{}) << false;
        QTest::newRow("unknown") << QJsonValue(QJsonArray{"chatgpt", "unknown"}) << false;
        QTest::newRow("wrong-type") << QJsonValue("chatgpt") << false;
    }
    void localSystemLayerAndLoginConstraints()
    {
        QFETCH(QJsonValue, methods); QFETCH(bool, accepted);
        auto config = safeConfig();
        config.insert("layers", QJsonArray{QJsonObject{{"name", QJsonObject{{"type", "system"}, {"file", "C:/fixture/config.toml"}}},
            {"config", QJsonObject{}}, {"version", "fixture"}}});
        const QJsonObject requirements{{"allowedLoginMethods", methods}};
        QCOMPARE(CodexRuntimeProfile::rejectionReasons(config, {{"requirements", requirements}}).isEmpty(), accepted);
        auto extraRequirement = requirements; extraRequirement.insert("hooks", QJsonObject{});
        QVERIFY(!CodexRuntimeProfile::rejectionReasons(config, {{"requirements", extraRequirement}}).isEmpty());
        auto unsafe = config.value("config").toObject(); auto features = unsafe.value("features").toObject();
        features.insert("shell_tool", true); unsafe.insert("features", features); config.insert("config", unsafe);
        QVERIFY(!CodexRuntimeProfile::rejectionReasons(config, {{"requirements", requirements}}).isEmpty());
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
        QVERIFY(CodexRuntimeProfile::acceptsLaunch(args.value())); QVERIFY(args.value().last().contains("\"name.with.dots\"={enabled=false}"));
        for (const auto& bad : {"mcp_servers={\"name\"={enabled=true}}", "mcp_servers={\"name\"={enabled=false},}", "mcp_servers={\"name\"={enabled=false,command=\"unsafe\"}}", "mcp_servers={\"name\"={enabled=false}},features.shell_tool=true"}) {
            auto malformed = CodexRuntimeProfile::launchArguments().value(); malformed << "-c" << bad;
            QVERIFY(!CodexRuntimeProfile::acceptsLaunch(malformed));
        }
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
    void experimentalDynamicToolCallUsesPinnedSchemaAndTypedRequestIds_data()
    {
        QTest::addColumn<QString>("version");
        for (const auto& version : CodexVersionSupport::supportedVersions())
            QTest::newRow(qPrintable(version)) << version;
    }
    void experimentalDynamicToolCallUsesPinnedSchemaAndTypedRequestIds()
    {
        QFETCH(QString, version);
        Session s;
        const auto specs = AssistantToolRuntime::experimentalToolSpecifications();
        QVERIFY(!specs.isEmpty());
        QVERIFY(s.client.configureDynamicTools(specs));
        QSignalSpy calls(&s.client, &ICodexClient::dynamicToolCallReceived);
        QVERIFY(s.client.launch(s.spec(1, CodexRuntimeProfile::launchArguments().value(), version)));
        s.reply("initialize", {{"codexHome", s.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        s.reply("account/read", chatGptAccount()); s.reply("config/read", safeConfig());
        s.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        QVERIFY(s.client.profileVerified());
        const auto initialize = s.transport.lastRequest(QStringLiteral("initialize"));
        QVERIFY(initialize.value(QStringLiteral("params")).toObject().value(QStringLiteral("capabilities")).toObject()
            .value(QStringLiteral("experimentalApi")).toBool());
        QVERIFY(s.openThread());
        const auto dynamic = s.transport.lastRequest(QStringLiteral("thread/start")).value(QStringLiteral("params")).toObject()
            .value(QStringLiteral("dynamicTools")).toArray();
        QCOMPARE(dynamic, specs);
        QVERIFY(s.client.threadBinding().profileFingerprint != CodexRuntimeProfile::fingerprint(QStringList{}));
        QVERIFY(s.beginTurn());

        QSignalSpy items(&s.client, &ICodexClient::itemChanged);
        QJsonObject toolItem{{"type", "dynamicToolCall"}, {"id", "tool-item"}, {"tool", "devhub_git_status"},
            {"arguments", QJsonObject{{"private", "PLANTED_TOOL_ARGUMENT"}}}, {"status", "inProgress"}};
        s.item("item/started", toolItem);
        QVERIFY(s.client.isInitialized()); QCOMPARE(items.size(), 1);
        QVERIFY(qvariant_cast<CodexItem>(items.first().at(0)).text.isEmpty());

        const QJsonObject request{{QStringLiteral("id"), 27}, {QStringLiteral("method"), QStringLiteral("item/tool/call")},
            {QStringLiteral("params"), QJsonObject{{QStringLiteral("threadId"), QStringLiteral("thread-1")},
                {QStringLiteral("turnId"), QStringLiteral("turn-1")}, {QStringLiteral("callId"), QStringLiteral("call-27")},
                {QStringLiteral("tool"), QStringLiteral("devhub_git_status")},
                {QStringLiteral("arguments"), QJsonObject{}}}}};
        s.transport.deliver(request);
        QCOMPARE(calls.size(), 1);
        const auto args = calls.front();
        QCOMPARE(args.at(0).value<QJsonValue>(), QJsonValue(27));
        QCOMPARE(args.at(1).toJsonObject().value(QStringLiteral("callId")).toString(), QStringLiteral("call-27"));
        QJsonObject response{{QStringLiteral("success"), true},
            {QStringLiteral("contentItems"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("inputText")},
                {QStringLiteral("text"), QStringLiteral("{\\\"success\\\":true,\\\"data\\\":{}}")}}}}};
        QVERIFY(s.client.resolveDynamicToolCall(QJsonValue(27), response));
        QVERIFY(!s.client.resolveDynamicToolCall(QJsonValue(27), response));
        QCOMPARE(s.transport.sent.back().value(QStringLiteral("id")), QJsonValue(27));
        QCOMPARE(s.transport.sent.back().value(QStringLiteral("result")), response);
        CodexWireContract wire(version);
        QVERIFY(wire.accepts(QStringLiteral("Response:DynamicToolCallResponse"), s.transport.sent.back().value(QStringLiteral("result"))));
        const auto sentCount = s.transport.sent.size();
        s.transport.deliver(request);
        QCOMPARE(calls.size(), 1);
        QCOMPARE(s.transport.sent.size(), sentCount);

        toolItem.insert("status", "completed"); toolItem.insert("success", true);
        toolItem.insert("contentItems", QJsonArray{QJsonObject{{"type", "inputText"}, {"text", "PLANTED_TOOL_BODY"}}});
        s.item("item/completed", toolItem);
        QCOMPARE(items.size(), 2); QVERIFY(qvariant_cast<CodexItem>(items.last().at(0)).text.isEmpty());
        QVERIFY(s.client.isInitialized());

        auto stringRequest = request;
        stringRequest.insert(QStringLiteral("id"), QStringLiteral("request-string-id"));
        stringRequest.insert(QStringLiteral("params"), QJsonObject{{QStringLiteral("threadId"), QStringLiteral("thread-1")},
            {QStringLiteral("turnId"), QStringLiteral("turn-1")}, {QStringLiteral("callId"), QStringLiteral("call-string")},
            {QStringLiteral("tool"), QStringLiteral("devhub_git_status")}, {QStringLiteral("arguments"), QJsonObject{}}});
        s.transport.deliver(stringRequest);
        QCOMPARE(calls.size(), 2);
        QVERIFY(s.client.resolveDynamicToolCall(QJsonValue(QStringLiteral("request-string-id")), response));
        QCOMPARE(s.transport.sent.back().value(QStringLiteral("id")), QJsonValue(QStringLiteral("request-string-id")));
        const QJsonArray terminalItems{toolItem, agent("final", "Done")};
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", turn("turn-1", "completed", terminalItems)}});
        QVERIFY(s.client.isInitialized()); QVERIFY(s.client.activeTurnId().isEmpty());
        QSignalSpy history(&s.client, &ICodexClient::threadReceived);
        QVERIFY(s.client.readThread(s.client.threadBinding()));
        s.reply("thread/read", s.threadResult(s.thread("thread-1", QJsonArray{turn("turn-1", "completed", terminalItems)})));
        QCOMPARE(history.size(), 1);
        const auto restored = qvariant_cast<CodexThread>(history.first().at(0));
        QCOMPARE(restored.turns.size(), 1); QCOMPARE(restored.turns.first().items.size(), 2);
        QVERIFY(restored.turns.first().items.first().text.isEmpty());
        QCOMPARE(restored.turns.first().items.last().text, QStringLiteral("Done"));
    }

    void persistedDynamicToolInventoryCannotRestoreUnderDifferentRegistry()
    {
        Session toolEnabled;
        QVERIFY(toolEnabled.client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications()));
        QVERIFY(toolEnabled.ready());
        QVERIFY(toolEnabled.openThread());
        const auto staleBinding = toolEnabled.client.threadBinding();
        QVERIFY(staleBinding.profileFingerprint != CodexRuntimeProfile::fingerprint(QStringList{}));

        Session currentRegistry;
        QVERIFY(currentRegistry.ready());
        QVERIFY(currentRegistry.openThread());
        auto stale = staleBinding;
        stale.accountFingerprint = currentRegistry.client.account().identityFingerprint;
        stale.codexHome = currentRegistry.client.threadBinding().codexHome;
        stale.scratchDirectory = currentRegistry.client.threadBinding().scratchDirectory;
        QVERIFY(!currentRegistry.client.resumeThread(stale));
        QVERIFY(currentRegistry.client.threadBinding().profileFingerprint
            == CodexRuntimeProfile::fingerprint(QStringList{}));
    }

    void dynamicToolCallsFailClosedForUnknownMalformedAndStaleScope()
    {
        Session s; QVERIFY(s.client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications()));
        QSignalSpy calls(&s.client, &ICodexClient::dynamicToolCallReceived);
        QVERIFY(s.ready()); QVERIFY(s.openThread()); QVERIFY(s.beginTurn());
        const auto deliver = [&s](QJsonValue id, QString thread, QString turnId, QString name, QJsonValue arguments) {
            s.transport.deliver({{QStringLiteral("id"), id}, {QStringLiteral("method"), QStringLiteral("item/tool/call")},
                {QStringLiteral("params"), QJsonObject{{QStringLiteral("threadId"), thread}, {QStringLiteral("turnId"), turnId},
                    {QStringLiteral("callId"), QStringLiteral("safe-call")}, {QStringLiteral("tool"), name}, {QStringLiteral("arguments"), arguments}}}});
        };
        deliver(1, QStringLiteral("wrong-thread"), QStringLiteral("turn-1"), QStringLiteral("devhub_git_status"), QJsonObject{});
        deliver(2, QStringLiteral("thread-1"), QStringLiteral("wrong-turn"), QStringLiteral("devhub_git_status"), QJsonObject{});
        QCOMPARE(calls.size(), 0);
        QVERIFY(!s.transport.sent.at(s.transport.sent.size() - 2).value(QStringLiteral("result")).toObject().value(QStringLiteral("success")).toBool());
        QVERIFY(!s.transport.sent.back().value(QStringLiteral("result")).toObject().value(QStringLiteral("success")).toBool());
        deliver(3, QStringLiteral("thread-1"), QStringLiteral("turn-1"), QStringLiteral("devhub_unknown"), QJsonObject{});
        QCOMPARE(calls.size(), 1); // The client validates association; registry rejects the unknown name.
        auto malformed = QJsonObject{{QStringLiteral("id"), 4}, {QStringLiteral("method"), QStringLiteral("item/tool/call")},
            {QStringLiteral("params"), QJsonObject{{QStringLiteral("threadId"), QStringLiteral("thread-1")}}}};
        s.transport.deliver(malformed);
        QCOMPARE(calls.size(), 1);
        QVERIFY(!s.transport.sent.back().value(QStringLiteral("result")).toObject().value(QStringLiteral("success")).toBool());
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
    void returnedThreadDirectoryScope_data()
    {
        QTest::addColumn<QString>("operation"); QTest::addColumn<bool>("outer");
        QTest::addColumn<QString>("variant"); QTest::addColumn<bool>("accepted");
        QStringList variants{"canonical", "other-directory", "relative", "missing"};
#ifdef Q_OS_WIN
        variants.append({"native-separators", "case", "extended-path"});
#endif
        for (const auto& operation : {QStringLiteral("thread/start"), QStringLiteral("thread/resume"), QStringLiteral("thread/read")})
            for (const bool outer : {false, true}) {
                if (outer && operation == "thread/read") continue;
                for (const auto& variant : variants) {
                    const auto name = (operation + (outer ? "/result/" : "/thread/") + variant).toUtf8();
                    QTest::newRow(name.constData()) << operation << outer << variant
                        << (variant == "canonical" || variant == "native-separators" || variant == "case" || variant == "extended-path");
                }
            }
    }
    void returnedThreadDirectoryScope()
    {
        QFETCH(QString, operation); QFETCH(bool, outer); QFETCH(QString, variant); QFETCH(bool, accepted);
        Session s; QTemporaryDir other; QVERIFY(other.isValid()); QVERIFY(s.ready());
        if (operation != "thread/start") QVERIFY(s.openThread());
        const auto binding = s.client.threadBinding();
        QSignalSpy received(&s.client, &CodexClient::threadReceived), failed(&s.client, &CodexClient::protocolFailed);
        const auto requested = operation == "thread/start" ? s.client.startThread("fixture-model")
            : operation == "thread/resume" ? s.client.resumeThread(binding) : s.client.readThread(binding);
        QVERIFY(requested);
        auto path = s.scratch.path();
        if (variant == "other-directory") path = other.path();
        else if (variant == "relative") path = QFileInfo(path).fileName();
        else if (variant == "missing") path = s.scratch.filePath("missing");
        else if (variant == "native-separators") path = QDir::toNativeSeparators(path);
        else if (variant == "case") path = path.toUpper();
        else if (variant == "extended-path") path = QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path);
        auto thread = s.thread(); auto result = s.threadResult(thread);
        if (outer) result.insert("cwd", path);
        else { thread.insert("cwd", path); result.insert("thread", thread); }
        if (operation == "thread/read") result = {{"thread", thread}};
        s.reply(operation, result);
        QCOMPARE(received.size(), accepted ? 1 : 0); QCOMPARE(failed.size(), accepted ? 0 : 1);
        QCOMPARE(s.client.isInitialized(), accepted);
        if (accepted) { QVERIFY(s.client.canSend()); QCOMPARE(s.client.threadBinding().scratchDirectory, QFileInfo(s.scratch.path()).canonicalFilePath()); }
    }
    void persistedEnvironmentAssignments_data()
    {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QString>("variant"); QTest::addColumn<QString>("version");
        for (const auto& version : {QStringLiteral("0.160.1"), QStringLiteral("0.162.0-alpha.17.2")})
        for (const auto& operation : {QStringLiteral("thread/start"), QStringLiteral("thread/resume"), QStringLiteral("thread/read")})
            for (const auto& variant : {QStringLiteral("null"), QStringLiteral("absent"), QStringLiteral("assigned")})
                QTest::newRow((version + '/' + operation + '/' + variant).toUtf8().constData()) << operation << variant << version;
    }
    void persistedEnvironmentAssignments()
    {
        QFETCH(QString, operation); QFETCH(QString, variant); QFETCH(QString, version);
        Session s; QVERIFY(s.ready(chatGptAccount(), version)); if (operation != "thread/start") QVERIFY(s.openThread());
        QSignalSpy received(&s.client, &CodexClient::threadReceived), failed(&s.client, &CodexClient::protocolFailed);
        const auto binding = s.client.threadBinding();
        QVERIFY(operation == "thread/start" ? s.client.startThread("fixture-model")
            : operation == "thread/resume" ? s.client.resumeThread(binding) : s.client.readThread(binding));
        auto thread = s.thread("thread-1", QJsonArray{turn("previous", "interrupted")});
        if (variant == "null") thread.insert("environments", QJsonValue(QJsonValue::Null));
        else if (variant == "absent") thread.remove("environments");
        else thread.insert("environments", QJsonArray{QJsonObject{{"environmentId", "foreign"}, {"cwd", "C:/foreign"}, {"runtimeWorkspaceRoots", QJsonArray{}}}});
        s.reply(operation, operation == "thread/read" ? QJsonObject{{"thread", thread}} : s.threadResult(thread));
        const bool accepted = operation != "thread/start" && variant != "assigned";
        QCOMPARE(received.size(), accepted ? 1 : 0); QCOMPARE(failed.size(), accepted ? 0 : 1);
        if (accepted) {
            QVERIFY(s.client.canSend()); QVERIFY(s.client.startTurn("fixture prompt"));
            QCOMPARE(s.transport.lastRequest("turn/start").value("params").toObject().value("environments"), QJsonValue(QJsonArray{}));
        }
    }
    void alphaResumeCompletePageAndScratchEnvironment_data()
    {
        QTest::addColumn<QString>("variant");
        for (const auto& value : {"complete", "legacy with full page", "unknown history mode", "no page", "next page", "too many turns", "duplicate turn", "summary items",
            "not loaded items", "active turn", "unsafe item", "foreign environment", "environment roots", "multiple environments",
            "wrong thread", "wrong cwd", "wrong provider", "writable sandbox", "network", "runtime roots"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void alphaResumeCompletePageAndScratchEnvironment()
    {
        QFETCH(QString, variant);
        CodexClientLimits limits; limits.historyTurns = 2;
        Session s(limits); QVERIFY(s.ready(chatGptAccount(), "0.162.0-alpha.17.2")); QVERIFY(s.openThread());
        QVERIFY(s.client.resumeThread(s.client.threadBinding()));
        QCOMPARE(s.transport.lastRequest("thread/resume").value("params").toObject().value("initialTurnsPage").toObject(),
            (QJsonObject{{"itemsView", "full"}, {"limit", 3}, {"sortDirection", "asc"}}));
        auto thread = s.thread(); thread.insert("historyMode", "paginated");
        if (variant == "legacy with full page") thread.insert("historyMode", "legacy");
        if (variant == "unknown history mode") thread.insert("historyMode", "unknown");
        QJsonObject environment{{"environmentId", "local"}, {"cwd", s.scratch.path()}, {"runtimeWorkspaceRoots", QJsonArray{}}};
        if (variant == "foreign environment") environment.insert("cwd", "C:/foreign");
        if (variant == "environment roots") environment.insert("runtimeWorkspaceRoots", QJsonArray{s.scratch.path()});
        thread.insert("environments", variant == "multiple environments" ? QJsonArray{environment, environment} : QJsonArray{environment});
        if (variant == "wrong thread") thread.insert("id", "other-thread");
        if (variant == "wrong cwd") thread.insert("cwd", "C:/foreign");
        if (variant == "wrong provider") thread.insert("modelProvider", "other-provider");
        auto historical = turn("previous", "interrupted", QJsonArray{agent("stored-agent", "restored text")});
        historical.insert("itemsView", "full");
        if (variant == "summary items") historical.insert("itemsView", "summary");
        if (variant == "not loaded items") historical.insert("itemsView", "notLoaded");
        if (variant == "active turn") historical.insert("status", "inProgress");
        if (variant == "unsafe item") historical.insert("items", QJsonArray{QJsonObject{{"type", "imageView"}, {"id", "image"}, {"path", "outside"}}});
        QJsonArray data{historical};
        if (variant == "duplicate turn") data.append(historical);
        if (variant == "too many turns") { data.append(turn("second", "completed")); data.append(turn("third", "completed")); }
        auto result = s.threadResult(thread);
        result.insert("initialTurnsPage", QJsonObject{{"data", data}, {"nextCursor", variant == "next page" ? QJsonValue("more") : QJsonValue(QJsonValue::Null)},
            {"backwardsCursor", "boundary"}});
        result.insert("turnsBackwardsCursor", "boundary"); result.insert("itemsBackwardsCursor", "item-boundary");
        if (variant == "no page") result.insert("initialTurnsPage", QJsonValue(QJsonValue::Null));
        if (variant == "writable sandbox") result.insert("sandbox", QJsonObject{{"type", "dangerFullAccess"}});
        if (variant == "network") result.insert("sandbox", QJsonObject{{"type", "readOnly"}, {"networkAccess", true}});
        if (variant == "runtime roots") result.insert("runtimeWorkspaceRoots", QJsonArray{s.scratch.path()});
        QSignalSpy received(&s.client, &CodexClient::threadReceived), failed(&s.client, &CodexClient::protocolFailed);
        s.reply("thread/resume", result);
        const bool accepted = variant == "complete" || variant == "legacy with full page";
        QCOMPARE(received.size(), accepted ? 1 : 0); QCOMPARE(failed.size(), accepted ? 0 : 1);
        if (accepted) {
            const auto restored = qvariant_cast<CodexThread>(received.first().first());
            QCOMPARE(restored.turns.size(), 1); QCOMPARE(restored.turns.first().items.first().text, QStringLiteral("restored text"));
            QVERIFY(s.client.canSend()); QVERIFY(s.client.startTurn("fixture prompt"));
            QCOMPARE(s.transport.lastRequest("turn/start").value("params").toObject().value("environments"), QJsonValue(QJsonArray{}));
        } else QVERIFY(!s.client.canSend());
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
        if (variant == "environments") value.insert("environments", QJsonArray{QJsonObject{
            {"environmentId", "foreign"}, {"cwd", "C:/foreign"}, {"runtimeWorkspaceRoots", QJsonArray{}}}});
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
        if (notification) {
            s.transport.event("account/updated", {{"authMode", "chatgpt"}});
            QVERIFY(s.client.isInitialized());
            s.reply("account/read", chatGptAccount("other-account"));
        }
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
    void unchangedAccountRefreshDefersTurnAcknowledgementAndStreaming_data()
    { QTest::addColumn<bool>("active"); QTest::newRow("before acknowledgement") << false; QTest::newRow("active turn") << true; }
    void unchangedAccountRefreshDefersTurnAcknowledgementAndStreaming()
    {
        QFETCH(bool, active); Session s; QVERIFY(s.ready(chatGptAccount(), "0.162.0-alpha.17.2")); QVERIFY(s.openThread());
        QVERIFY(s.client.startTurn("fixture prompt"));
        auto live = turn(); live.insert("itemsView", "notLoaded");
        if (active) s.reply("turn/start", {{"turn", live}});
        QSignalSpy accepted(&s.client, &CodexClient::turnAccepted), finished(&s.client, &CodexClient::turnFinished);
        QSignalSpy items(&s.client, &CodexClient::itemChanged), failures(&s.client, &CodexClient::protocolFailed);
        s.transport.event("account/updated", {{"authMode", "chatgpt"}, {"planType", "plus"}});
        const auto obsolete = s.requestId("account/read");
        QCOMPARE(s.client.account().mode, CodexAccountMode::Unknown); QVERIFY(!s.client.canSend());
        QVERIFY(!s.client.readThread(s.client.threadBinding()));
        // Repeated notifications replace account checks without confirming the
        // old response or dropping the already acknowledged turn.
        s.transport.event("account/updated", {{"authMode", "chatgpt"}, {"planType", "pro"}});
        s.transport.reply(obsolete, chatGptAccount("stale-account"));
        if (!active) s.reply("turn/start", {{"turn", live}});
        s.delta("buffered text");
        auto completed = turn("turn-1", "completed"); completed.insert("itemsView", "notLoaded");
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", completed}});
        QCOMPARE(items.size(), 0); QCOMPARE(finished.size(), 0); QCOMPARE(accepted.size(), 0);
        auto account = chatGptAccount(); auto fields = account.value("account").toObject(); fields.insert("planType", "pro"); account.insert("account", fields);
        s.reply("account/read", account);
        QCOMPARE(failures.size(), 0); QCOMPARE(accepted.size(), active ? 0 : 1); QCOMPARE(finished.size(), 1);
        const auto result = qvariant_cast<CodexTurn>(finished.first().first());
        QCOMPARE(result.status, CodexTurnStatus::Completed); QCOMPARE(result.items.first().text, QStringLiteral("buffered text"));
        QCOMPARE(s.client.account().planType, QStringLiteral("pro")); QVERIFY(s.client.canSend());
        QCOMPARE(s.transport.stops, 0);
    }
    void accountRefreshRevocationDropsBufferedTraffic_data()
    {
        QTest::addColumn<QString>("variant");
        for (const auto& value : {"different identity", "signed out", "auth disabled", "read error", "deadline", "buffer cap"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void accountRefreshRevocationDropsBufferedTraffic()
    {
        QFETCH(QString, variant); CodexClientLimits limits;
        if (variant == "deadline") limits.acknowledgementMilliseconds = 25;
        if (variant == "buffer cap") limits.earlyEvents = 0;
        Session s(limits); QVERIFY(s.ready(chatGptAccount(), "0.162.0-alpha.17.2")); QVERIFY(s.openThread()); QVERIFY(s.beginTurn());
        QSignalSpy items(&s.client, &CodexClient::itemChanged), finished(&s.client, &CodexClient::turnFinished);
        s.transport.event("account/updated", {{"authMode", "chatgpt"}}); s.delta("must remain undisclosed");
        if (variant == "different identity") s.reply("account/read", chatGptAccount("other-account"));
        if (variant == "signed out") s.reply("account/read", {{"account", QJsonValue(QJsonValue::Null)}, {"requiresOpenaiAuth", true}});
        if (variant == "auth disabled") { auto result = chatGptAccount(); result.insert("requiresOpenaiAuth", false); s.reply("account/read", result); }
        if (variant == "read error") s.transport.deliver({{"id", s.requestId("account/read")}, {"error", QJsonObject{{"code", -32603}, {"message", "PRIVATE_ERROR"}}}});
        if (variant == "deadline") QTRY_COMPARE_WITH_TIMEOUT(s.transport.stops, 1, 1000);
        QCOMPARE(items.size(), 0); QCOMPARE(finished.size(), 1); QCOMPARE(s.transport.stops, 1);
        QCOMPARE(qvariant_cast<CodexTurn>(finished.first().first()).status, CodexTurnStatus::Uncertain);
        QVERIFY(!s.client.canSend()); QVERIFY(!s.client.isInitialized());
    }
    void accountRefreshDefersDynamicToolInvocation()
    {
        Session s; QVERIFY(s.client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications()));
        QVERIFY(s.ready(chatGptAccount(), "0.162.0-alpha.17.2")); QVERIFY(s.openThread()); QVERIFY(s.beginTurn());
        QSignalSpy calls(&s.client, &CodexClient::dynamicToolCallReceived);
        s.transport.event("account/updated", {{"authMode", "chatgpt"}});
        s.transport.deliver({{"id", 27}, {"method", "item/tool/call"}, {"params", QJsonObject{
            {"threadId", "thread-1"}, {"turnId", "turn-1"}, {"callId", "call-27"}, {"tool", "devhub_git_status"}, {"arguments", QJsonObject{}}}}});
        QCOMPARE(calls.size(), 0);
        s.reply("account/read", chatGptAccount()); QCOMPARE(calls.size(), 1); QCOMPARE(s.transport.stops, 0);
    }
    void alphaLiveUnloadedTurnsRetainStreamedItems_data()
    { QTest::addColumn<bool>("early"); QTest::newRow("acknowledgement first") << false; QTest::newRow("early events") << true; }
    void accountRefreshHoldsToolResultsUntilIdentityIsVerified_data()
    { QTest::addColumn<bool>("changed"); QTest::newRow("unchanged") << false; QTest::newRow("changed") << true; }
    void accountRefreshHoldsToolResultsUntilIdentityIsVerified()
    {
        QFETCH(bool, changed); Session s; QVERIFY(s.client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications()));
        QVERIFY(s.ready(chatGptAccount(), "0.162.0-alpha.17.2")); QVERIFY(s.openThread()); QVERIFY(s.beginTurn());
        s.transport.deliver({{"id", 27}, {"method", "item/tool/call"}, {"params", QJsonObject{
            {"threadId", "thread-1"}, {"turnId", "turn-1"}, {"callId", "call-27"}, {"tool", "devhub_git_status"}, {"arguments", QJsonObject{}}}}});
        s.transport.event("account/updated", {{"authMode", "chatgpt"}});
        const auto requests = s.transport.sent.size();
        const QJsonObject result{{"success", true}, {"contentItems", QJsonArray{QJsonObject{{"type", "inputText"}, {"text", "fixture tool output"}}}}};
        QVERIFY(s.client.resolveDynamicToolCall(QJsonValue(27), result));
        QVERIFY(!s.client.resolveDynamicToolCall(QJsonValue(27), result)); QCOMPARE(s.transport.sent.size(), requests);
        s.reply("account/read", chatGptAccount(changed ? "other-account" : "fixture-account"));
        QCOMPARE(s.transport.sent.size(), requests + (changed ? 0 : 1));
        if (!changed) QCOMPARE(s.transport.sent.last(), (QJsonObject{{"id", 27}, {"result", result}}));
        QCOMPARE(s.client.isInitialized(), !changed);
    }
    void alphaLiveUnloadedTurnsRetainStreamedItems()
    {
        QFETCH(bool, early); Session s; QVERIFY(s.ready(chatGptAccount(), "0.162.0-alpha.17.2")); QVERIFY(s.openThread());
        QSignalSpy finished(&s.client, &CodexClient::turnFinished); QVERIFY(s.client.startTurn("fixture prompt"));
        auto live = turn(); live.insert("itemsView", "notLoaded");
        if (!early) s.reply("turn/start", {{"turn", live}});
        s.transport.event("turn/started", {{"threadId", "thread-1"}, {"turn", live}});
        s.delta("streamed answer"); s.item("item/completed", agent("agent-1", "streamed answer"));
        auto completed = turn("turn-1", "completed"); completed.insert("itemsView", "notLoaded");
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", completed}});
        if (early) s.reply("turn/start", {{"turn", live}});
        QCOMPARE(finished.size(), 1); QCOMPARE(qvariant_cast<CodexTurn>(finished.first().first()).items.first().text, QStringLiteral("streamed answer"));
        QVERIFY(s.client.canSend()); QCOMPARE(s.transport.stops, 0);
    }
    void unloadedTurnsRemainStrictOutsideLiveAlpha_data()
    {
        QTest::addColumn<QString>("variant");
        for (const auto& value : {"summary", "unloaded with items", "legacy live", "stored history"}) QTest::newRow(value) << QString::fromLatin1(value);
    }
    void alphaSuccessfulSummaryMergesFinalAnswer_data()
    {
        QTest::addColumn<bool>("early"); QTest::addColumn<bool>("streamedFinal");
        QTest::newRow("early completion") << true << true;
        QTest::newRow("completed item") << false << true;
        QTest::newRow("summary supplies final text") << false << false;
    }
    void alphaSuccessfulSummaryMergesFinalAnswer()
    {
        QFETCH(bool, early); QFETCH(bool, streamedFinal);
        Session s; QVERIFY(s.client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications()));
        QVERIFY(s.ready(chatGptAccount(), "0.162.0-alpha.17.2")); QVERIFY(s.openThread());
        QSignalSpy finished(&s.client, &CodexClient::turnFinished);
        QVERIFY(s.client.startTurn("fixture prompt")); auto started = turn(); started.insert("itemsView", "notLoaded");
        if (!early) s.reply("turn/start", {{"turn", started}});
        s.item("item/completed", agent("commentary", "Checking files."));
        s.item("item/completed", {{"type", "dynamicToolCall"}, {"id", "tool-item"}, {"tool", "devhub_files_search"},
            {"namespace", QJsonValue::Null}, {"arguments", QJsonObject{}}, {"status", "completed"}});
        if (streamedFinal) s.item("item/completed", agent("final", "Done."));
        auto completed = turn("turn-1", "completed", QJsonArray{agent("final", "Done.")}); completed.insert("itemsView", "summary");
        s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", completed}});
        if (early) s.reply("turn/start", {{"turn", started}});
        QCOMPARE(finished.size(), 1); const auto result = qvariant_cast<CodexTurn>(finished.first().first());
        QCOMPARE(result.status, CodexTurnStatus::Completed);
        QCOMPARE(result.items.size(), 3); QCOMPARE(result.items.last().text, QStringLiteral("Done."));
        QVERIFY(s.client.canSend()); QCOMPARE(s.transport.stops, 0);
    }
    void alphaSummaryStillRejectsInvalidTerminalPayloads_data()
    {
        QTest::addColumn<QString>("variant");
        for (const auto& value : {"active", "error", "unsafe", "multiple", "conflicting text", "old runtime", "stored summary"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void alphaSummaryStillRejectsInvalidTerminalPayloads()
    {
        QFETCH(QString, variant); Session s;
        QVERIFY(s.ready(chatGptAccount(), variant == "old runtime" ? "0.160.1" : "0.162.0-alpha.17.2")); QVERIFY(s.openThread());
        auto completed = turn("turn-1", "completed", QJsonArray{agent("final", "Done.")}); completed.insert("itemsView", "summary");
        if (variant == "active") completed.insert("status", "inProgress");
        if (variant == "error") completed.insert("error", QJsonObject{{"message", "failure"}, {"codexErrorInfo", "other"}});
        if (variant == "unsafe") completed.insert("items", QJsonArray{QJsonObject{{"type", "commandExecution"}, {"id", "command"}}});
        if (variant == "multiple") completed.insert("items", QJsonArray{agent("first", "A"), agent("second", "B")});
        QSignalSpy failed(&s.client, &CodexClient::protocolFailed);
        if (variant == "stored summary") {
            QVERIFY(s.client.readThread(s.client.threadBinding())); s.reply("thread/read", {{"thread", s.thread("thread-1", QJsonArray{completed})}});
        } else {
            QVERIFY(s.beginTurn());
            if (variant == "conflicting text") s.item("item/completed", agent("final", "Different."));
            s.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", completed}});
        }
        QCOMPARE(failed.size(), 1); QVERIFY(!s.client.canSend());
    }
    void unloadedTurnsRemainStrictOutsideLiveAlpha()
    {
        QFETCH(QString, variant); Session s;
        QVERIFY(s.ready(chatGptAccount(), variant == "legacy live" ? "0.160.1" : "0.162.0-alpha.17.2")); QVERIFY(s.openThread());
        auto value = turn("turn-1", variant == "stored history" ? "completed" : "inProgress");
        value.insert("itemsView", variant == "summary" ? "summary" : "notLoaded");
        if (variant == "unloaded with items") value.insert("items", QJsonArray{agent("agent-1", "invalid")});
        QSignalSpy failed(&s.client, &CodexClient::protocolFailed);
        if (variant == "stored history") { QVERIFY(s.client.readThread(s.client.threadBinding())); s.reply("thread/read", {{"thread", s.thread("thread-1", QJsonArray{value})}}); }
        else { QVERIFY(s.client.startTurn("fixture prompt")); s.reply("turn/start", {{"turn", value}}); }
        QCOMPARE(failed.size(), 1); QVERIFY(!s.client.canSend());
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
