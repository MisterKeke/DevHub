#include "support/AssistantHarness.h"
#include "application/events/ApplicationEventHub.h"
#include "application/lifecycle/ApplicationLifecycle.h"
#include "application/lifecycle/ShutdownCoordinator.h"
#include "application/tasks/TaskScheduler.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTimer>
#include <QTest>
using namespace devhub::application::assistant;
namespace application = devhub::application;
using namespace devhub::tests::assistant;
using namespace devhub::tests::codex;
using devhub::domain::errors::ErrorCode;
using devhub::domain::errors::AppError;
using devhub::domain::errors::ErrorCategory;
class AssistantControllerTest final : public QObject {
    Q_OBJECT
private slots:
    void reconnectReconcilesExistingTranscript_data() {
        QTest::addColumn<bool>("persist"); QTest::addColumn<bool>("uncertain");
        QTest::newRow("session-only uncertain") << false << true;
        QTest::newRow("persisted uncertain after New Chat") << true << true;
        QTest::newRow("completed current conversation") << true << false;
    }
    void reconnectReconcilesExistingTranscript() {
        QFETCH(bool, persist); QFETCH(bool, uncertain);
        MemoryConversationRepository repository; Harness h({}, {}, persist ? &repository : nullptr, "0.162.0-alpha.17.2");
        h.controller.setPersistenceContext("local-account", persist); QVERIFY(h.readyController()); h.controller.newChat();
        QVERIFY(h.sendAndAccept("question")); h.delta("answer");
        if (uncertain) { auto invalid = turn("turn-1", "completed"); invalid.insert("itemsView", "unknown");
            h.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", invalid}});
            QCOMPARE(h.controller.operation(), AssistantOperation::Uncertain);
        } else h.finish("completed", QJsonArray{agent("agent-1", "answer")});
        const auto conversationId = h.controller.conversation().id;
        const auto turnRequest = h.requestId("turn/start"); QSignalSpy reset(&h.controller, &AssistantController::transcriptReset);
        h.controller.reconnect(); QVERIFY(h.readyController());
        QCOMPARE(h.controller.operation(), AssistantOperation::Restoring); QVERIFY(!h.controller.canSend());
        const QJsonObject user{{"type", "userMessage"}, {"id", "user-1"}, {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "question"}}}}};
        h.reply("thread/resume", h.threadResult(h.thread("thread-1", QJsonArray{turn("turn-1", "completed", QJsonArray{user, agent("agent-1", "answer")})})));
        QCOMPARE(h.controller.operation(), AssistantOperation::Idle); QVERIFY(h.controller.canSend());
        QCOMPARE(h.controller.messages().size(), 2); QCOMPARE(reset.size(), 1);
        QCOMPARE(h.controller.conversation().id, conversationId); QVERIFY(h.controller.errorText().isEmpty());
        QCOMPARE(h.controller.messages().first().delivery, AssistantDelivery::Completed);
        QCOMPARE(h.controller.messages().last().delivery, AssistantDelivery::Completed);
        QCOMPARE(h.requestId("turn/start"), turnRequest);
        if (persist) QCOMPARE(repository.rows.last().lastTurnStatus, QStringLiteral("completed"));
    }
    void reconnectRetainsTranscriptWhenHistoryCannotReconcile_data() {
        QTest::addColumn<QString>("variant");
        for (const auto& value : {"missing turn", "active turn", "foreign account", "read failure"}) QTest::newRow(value) << QString::fromLatin1(value);
    }
    void reconnectRetainsTranscriptWhenHistoryCannotReconcile() {
        QFETCH(QString, variant); Harness h({}, {}, nullptr, "0.162.0-alpha.17.2"); QVERIFY(h.readyController());
        QVERIFY(h.sendAndAccept("question")); h.delta("partial answer");
        auto invalid = turn("turn-1", "completed"); invalid.insert("itemsView", "unknown");
        h.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", invalid}});
        const auto turnRequest = h.requestId("turn/start"); const auto resumeRequest = h.requestId("thread/resume");
        QSignalSpy reset(&h.controller, &AssistantController::transcriptReset);
        h.controller.reconnect(); QVERIFY(h.readyController());
        if (variant == "foreign account") {
            QVERIFY(h.client.readAccount()); h.reply("account/read", chatGptAccount("foreign-account"));
        } else if (variant == "read failure") h.transport.deliver({{"id", h.requestId("thread/resume")}, {"error", QJsonObject{{"code", -32603}, {"message", "missing"}}}});
        else h.reply("thread/resume", h.threadResult(h.thread("thread-1", variant == "missing turn" ? QJsonArray{} : QJsonArray{turn()})));
        QVERIFY(!h.controller.canSend()); QCOMPARE(h.controller.messages().size(), 2); QCOMPARE(reset.size(), 0);
        QCOMPARE(h.controller.messages().last().source, QStringLiteral("partial answer"));
        QCOMPARE(h.requestId("turn/start"), turnRequest);
        Q_UNUSED(resumeRequest);
    }
    void scopeFailureStaysVisibleAndReconnectRecovers() {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.controller.send("fixture"));
        auto result = h.threadResult(); result.insert("cwd", "C:/outside-the-scratch-directory");
        h.reply("thread/start", result);
        QCOMPARE(h.controller.connection(), AssistantConnectionState::Failed);
        QVERIFY(h.controller.statusText().contains("thread/start")); QVERIFY(!h.controller.canSend());
        h.controller.reconnect(); QVERIFY(h.readyController()); QVERIFY(h.controller.canSend());
        QVERIFY(h.controller.errorText().isEmpty());
    }
    void initializationRejectionLeavesStartingAndCanReconnect() {
        Harness h; h.controller.open();
        QCOMPARE(h.controller.connection(), AssistantConnectionState::Starting);
        h.transport.deliver({{"id", h.requestId("initialize")}, {"error", QJsonObject{
            {"code", -32603}, {"message", "PRIVATE_RUNTIME_DETAILS"}}}});
        QCOMPARE(h.transport.stops, 1);
        QCOMPARE(h.client.pendingRequestCount(), 0);
        QCOMPARE(h.controller.connection(), AssistantConnectionState::Failed);
        QVERIFY(h.controller.statusText().contains("initialization was rejected"));
        QVERIFY(!h.controller.statusText().contains("PRIVATE"));
        QVERIFY(h.controller.canReconnect()); QVERIFY(!h.controller.canSend());
        h.controller.reconnect(); QVERIFY(h.readyController()); QVERIFY(h.controller.canSend());
    }
    void accountRefreshAndNativeUnloadedTurnDoNotMakeDeliveryUncertain() {
        Harness h({}, {}, nullptr, "0.162.0-alpha.17.2"); QVERIFY(h.readyController());
        QVERIFY(h.controller.send("fixture prompt")); h.reply("thread/start", h.threadResult());
        QCOMPARE(h.controller.operation(), AssistantOperation::Submitting);
        QVERIFY(h.client.readAccount());
        h.transport.event("account/updated", {{"authMode", "chatgpt"}, {"planType", "plus"}});
        QVERIFY(h.controller.errorText().isEmpty());
        auto live = turn(); live.insert("itemsView", "notLoaded"); h.reply("turn/start", {{"turn", live}});
        QCOMPARE(h.controller.operation(), AssistantOperation::Submitting);
        h.transport.event("turn/started", {{"threadId", "thread-1"}, {"turn", live}});
        h.delta("streamed answer");
        auto completed = turn("turn-1", "completed", QJsonArray{agent("agent-1", "streamed answer")}); completed.insert("itemsView", "summary");
        h.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", completed}});
        QCOMPARE(h.controller.messages().size(), 1);
        h.reply("account/read", chatGptAccount());
        QCOMPARE(h.controller.operation(), AssistantOperation::Idle);
        QCOMPARE(h.controller.messages().size(), 2);
        QCOMPARE(h.controller.messages().first().delivery, AssistantDelivery::Completed);
        QCOMPARE(h.controller.messages().last().source, QStringLiteral("streamed answer"));
        QVERIFY(h.controller.errorText().isEmpty()); QVERIFY(h.controller.canSend());
        QCOMPARE(h.transport.stops, 0);
    }
    void newChatRecoversFailedResumeWithoutRestoringAgain() {
        MemoryConversationRepository repository;
        {
            Harness original({}, {}, &repository, "0.162.0-alpha.17.2");
            original.controller.setPersistenceContext("local-account", true); QVERIFY(original.readyController());
            QVERIFY(original.sendAndAccept("saved question")); original.finish();
        }
        Harness h({}, {}, &repository, "0.162.0-alpha.17.2");
        repository.rows.first().binding.codexHomeKey = QString::fromLatin1(
            QCryptographicHash::hash(h.scratch.path().toUtf8(), QCryptographicHash::Sha256).toHex());
        h.controller.setPersistenceContext("local-account", true); QVERIFY(h.readyController());
        QCOMPARE(h.controller.operation(), AssistantOperation::Restoring);
        auto result = h.threadResult(); result.insert("cwd", "C:/foreign"); h.reply("thread/resume", result);
        QCOMPARE(h.controller.connection(), AssistantConnectionState::Failed); QVERIFY(!h.controller.canSend());
        const auto failedConversation = h.controller.conversation().id;
        const auto resumeId = h.requestId("thread/resume");
        h.controller.newChat();
        QCOMPARE(h.controller.connection(), AssistantConnectionState::Starting);
        QVERIFY(h.controller.conversation().id != failedConversation); QVERIFY(h.controller.messages().isEmpty());
        QVERIFY(h.readyController()); QCOMPARE(h.controller.operation(), AssistantOperation::Idle);
        QVERIFY(h.controller.canSend()); QVERIFY(h.controller.errorText().isEmpty());
        QCOMPARE(h.requestId("thread/resume"), resumeId); QCOMPARE(repository.rows.size(), 1);
        // A later manual reconnect must also retain the user's empty New Chat.
        h.controller.reconnect(); QVERIFY(h.readyController()); QVERIFY(h.controller.canSend());
        QCOMPARE(h.requestId("thread/resume"), resumeId);
        QVERIFY(h.sendAndAccept("new question")); QCOMPARE(repository.rows.size(), 2);
    }
    void startupDeadlineStopsUnresponsiveBackendAndRecovers() {
        AssistantLimits limits; limits.startupMilliseconds = 25;
        Harness h(limits); h.controller.open();
        QTRY_COMPARE_WITH_TIMEOUT(h.controller.connection(), AssistantConnectionState::Failed, 1000);
        QCOMPARE(h.transport.stops, 1); QVERIFY(!h.client.isInitialized());
        QVERIFY(h.controller.statusText().contains("startup timed out"));
        QVERIFY(h.controller.canReconnect()); QVERIFY(!h.controller.canSend());
        h.controller.reconnect(); QVERIFY(h.readyController()); QVERIFY(h.controller.canSend());
        QTest::qWait(40); QCOMPARE(h.transport.stops, 1); QVERIFY(h.controller.canSend());
    }
    void reconnectWhileStartingInvalidatesOldDeadlineAndResponse() {
        AssistantLimits limits; limits.startupMilliseconds = 50;
        Harness h(limits); h.controller.open();
        const auto oldGeneration = h.client.processGeneration();
        const auto oldInitialize = h.requestId("initialize");
        h.controller.reconnect(); QCOMPARE(h.transport.stops, 1);
        emit h.transport.objectReceived({{"id", oldInitialize}, {"result", QJsonObject{}}}, oldGeneration);
        QVERIFY(h.readyController()); QVERIFY(h.controller.canSend());
        QTest::qWait(60); QCOMPARE(h.transport.stops, 1); QVERIFY(h.controller.canSend());
    }
    void inheritedMcpIsDisabledInBoundedChildRelaunch() {
        // The in-memory fixture exercises the same config/relaunch policy. It
        // cannot impersonate a native executable with measured schema evidence.
        Harness h({}, {}, nullptr, "0.162.0-alpha.17.2");
        h.controller.open();
        h.reply("initialize", {{"codexHome", h.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        h.reply("account/read", chatGptAccount());
        auto result = safeConfig(); auto config = result.value("config").toObject();
        config.insert("mcp_servers", QJsonObject{{"inherited.name", QJsonObject{{"enabled", true}}}}); result.insert("config", config);
        h.reply("config/read", result); h.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        QVERIFY(!h.controller.canSend());
        QTRY_COMPARE(h.transport.launches.size(), 2);
        QCOMPARE(h.transport.stops, 1);
        const auto restricted = h.transport.launches.last();
        QVERIFY(restricted.arguments.contains("mcp_servers={\"inherited.name\"={enabled=false}}"));
        QCOMPARE(restricted.environment, h.transport.launches.first().environment);
        QCOMPARE(restricted.scratchDirectory, h.scratch.path());
        QVERIFY(h.readyController()); QVERIFY(h.controller.canSend());
        QVERIFY(h.controller.statusText().contains("ready"));
        QVERIFY(h.client.verifyRuntime());
        config.insert("mcp_servers", QJsonObject{{"new-server", QJsonObject{{"enabled", true}}}}); result.insert("config", config);
        h.reply("config/read", result); h.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        QTest::qWait(10); QCOMPARE(h.transport.launches.size(), 2); QVERIFY(!h.controller.canSend());
        h.controller.reconnect(); QCOMPARE(h.transport.launches.size(), 3);
        QCOMPARE(h.transport.launches.last().arguments, devhub::infrastructure::codex::CodexRuntimeProfile::launchArguments().value());
        QVERIFY(h.readyController()); QVERIFY(h.controller.canSend());
    }
    void profileRetryCannotReopenLockedSession() {
        Harness h; h.controller.open();
        h.reply("initialize", {{"codexHome", h.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        auto result = safeConfig(); auto config = result.value("config").toObject();
        config.insert("mcp_servers", QJsonObject{{"inherited", QJsonObject{}}}); result.insert("config", config);
        h.reply("config/read", result); h.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        h.controller.setSessionAllowed(false); QTest::qWait(10);
        QCOMPARE(h.transport.launches.size(), 1); QVERIFY(!h.controller.canSend()); QVERIFY(!h.controller.canReconnect());
    }
    void profileRejectionExplainsChecksWithoutConfigurationValues() {
        Harness h; h.controller.open();
        h.reply("initialize", {{"codexHome", h.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        h.reply("account/read", chatGptAccount());
        auto result = safeConfig(); auto config = result.value("config").toObject();
        config.insert("notify", QJsonArray{"PRIVATE_COMMAND_TOKEN"});
        config.insert("mcp_servers", QJsonObject{{"PRIVATE_SERVER", QJsonObject{{"enabled", true}}}});
        config.insert("desktop", QJsonObject{{"PRIVATE_KEY", "PRIVATE_VALUE"}});
        result.insert("config", config);
        h.reply("config/read", result); h.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        QVERIFY(!h.controller.canSend());
        QVERIFY(h.controller.statusText().contains("notification command"));
        QVERIFY(h.controller.statusText().contains("MCP servers"));
        QVERIFY(h.controller.statusText().contains("desktop/browser"));
        QVERIFY(!h.controller.statusText().contains("PRIVATE"));
        const auto diagnostics = h.controller.diagnosticsSnapshot().join('\n');
        QVERIFY(diagnostics.contains("MCP servers")); QVERIFY(!diagnostics.contains("PRIVATE"));
        emit h.client.profileChecked(false, {"PRIVATE_UNKNOWN_REASON"});
        QVERIFY(!h.controller.statusText().contains("PRIVATE"));
        emit h.client.profileChecked(false, {"mcp-inventory-unverified"});
        QVERIFY(h.controller.statusText().contains("MCP server restrictions could not be verified"));
        QVERIFY(!h.controller.statusText().contains("still active"));
        h.controller.reconnect();
        QVERIFY(!h.controller.diagnosticsSnapshot().join('\n').contains("MCP servers"));
    }
    void shutdownCoordinatorWaitsForParticipantAndBoundsTimeout()
    {
        {
            application::events::ApplicationEventHub events;
            application::lifecycle::ApplicationLifecycle lifecycle(events);
            QVERIFY(lifecycle.transitionTo(application::lifecycle::ApplicationLifecycleState::Ready));
            SequenceIds ids; application::tasks::TaskScheduler scheduler(ids, 1);
            application::lifecycle::ShutdownCoordinator shutdown(*QCoreApplication::instance(), lifecycle,
                scheduler, events, nullptr, 100);
            QObject participant; QSignalSpy finished(&shutdown, &application::lifecycle::ShutdownCoordinator::shutdownFinished);
            shutdown.addPreQuitParticipant(&participant, [&participant](auto completion) {
                QTimer::singleShot(0, &participant, [completion = std::move(completion)] { completion(); });
            });
            shutdown.requestShutdown();
            QCOMPARE(lifecycle.state(), application::lifecycle::ApplicationLifecycleState::ShutdownRequested);
            QVERIFY(!scheduler.isAccepting());
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 1000);
            QCOMPARE(lifecycle.state(), application::lifecycle::ApplicationLifecycleState::Stopped);
        }
        {
            application::events::ApplicationEventHub events;
            application::lifecycle::ApplicationLifecycle lifecycle(events);
            QVERIFY(lifecycle.transitionTo(application::lifecycle::ApplicationLifecycleState::Ready));
            SequenceIds ids; application::tasks::TaskScheduler scheduler(ids, 1);
            application::lifecycle::ShutdownCoordinator shutdown(*QCoreApplication::instance(), lifecycle,
                scheduler, events, nullptr, 25);
            QObject participant; application::lifecycle::ShutdownCoordinator::Completion stale;
            shutdown.addPreQuitParticipant(&participant, [&stale](auto completion) { stale = std::move(completion); });
            QSignalSpy finished(&shutdown, &application::lifecycle::ShutdownCoordinator::shutdownFinished);
            shutdown.requestShutdown();
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 1000);
            QCOMPARE(lifecycle.state(), application::lifecycle::ApplicationLifecycleState::Stopped);
            stale(); QCoreApplication::processEvents();
            QCOMPARE(finished.size(), 1);
        }
    }
    void lazyProbeAndThread()
    {
        Harness h; QCOMPARE(h.transport.sent.size(), 0); QVERIFY(h.readyController()); QVERIFY(h.controller.canSend());
        QVERIFY(h.transport.lastRequest("thread/start").isEmpty()); QVERIFY(h.controller.send("first"));
        QCOMPARE(h.controller.operation(), AssistantOperation::CreatingThread); QVERIFY(!h.controller.send("duplicate"));
        QVERIFY(h.transport.lastRequest("turn/start").isEmpty()); QSignalSpy admitted(&h.controller, &AssistantController::sendAdmitted);
        h.reply("thread/start", h.threadResult()); QCOMPARE(admitted.size(), 1); QCOMPARE(h.controller.messages().size(), 1);
        QCOMPARE(h.controller.messages().front().delivery, AssistantDelivery::Queued);
        h.reply("turn/start", {{"turn", turn()}}); QCOMPARE(h.controller.messages().front().delivery, AssistantDelivery::Accepted);
        QVERIFY(!h.controller.messages().front().id.isNull()); QVERIFY(h.controller.messages().front().id != h.controller.conversation().id);
    }
    void metadataSavedAfterThreadIsConfirmedAndSqlFailureKeepsChatLive()
    {
        MemoryConversationRepository repository; Harness h({}, {}, &repository);
        h.controller.setPersistenceContext(QStringLiteral("local-account"), true);
        QVERIFY(h.readyController()); QVERIFY(h.controller.send("persist this title"));
        QVERIFY(repository.rows.isEmpty());
        h.reply("thread/start", h.threadResult());
        QCOMPARE(repository.rows.size(), 1); QCOMPARE(repository.rows.front().binding.threadId, QStringLiteral("thread-1"));
        QCOMPARE(repository.rows.front().conversation.title, QStringLiteral("persist this title"));
        QVERIFY(repository.rows.front().createdAtUtc.isValid());
        QVERIFY(h.controller.operation() == AssistantOperation::Submitting);

        MemoryConversationRepository failedRepository; failedRepository.failSave = true;
        Harness live({}, {}, &failedRepository); live.controller.setPersistenceContext(QStringLiteral("local-account"), true);
        QVERIFY(live.readyController()); QVERIFY(live.controller.send("keep live chat")); live.reply("thread/start", live.threadResult());
        QCOMPARE(live.controller.operation(), AssistantOperation::Submitting);
        QCOMPARE(live.controller.messages().front().source, QStringLiteral("keep live chat"));
        QVERIFY(!live.controller.errorText().isEmpty());
        live.reply("turn/start", {{"turn", turn()}}); QCOMPARE(live.controller.operation(), AssistantOperation::Streaming);
    }
    void restoreHydratesThreadHistoryOnceAndRejectsIncompatibleProfile()
    {
        MemoryConversationRepository repository;
        {
            Harness original({}, {}, &repository); original.controller.setPersistenceContext(QStringLiteral("local-account"), true);
            original.controller.setContext({"workspace-x", "Original workspace label", "C:/fixture/workspace"});
            QVERIFY(original.readyController()); QVERIFY(original.controller.send("first restored question"));
            original.reply("thread/start", original.threadResult()); original.reply("turn/start", {{"turn", turn()}});
            original.finish("completed", QJsonArray{agent("agent-1", "restored answer")});
            QCOMPARE(repository.rows.size(), 1);
        }
        auto& stored = repository.rows.front();
        stored.conversation.context.name.clear(); // SQL stores workspace identity/root, not its mutable display label.
        Harness restored({}, {}, &repository); stored.binding.codexHomeKey = QString::fromLatin1(
            QCryptographicHash::hash(restored.scratch.path().toUtf8(), QCryptographicHash::Sha256).toHex());
        restored.controller.setContext({"workspace-x", "Renamed workspace", "c:/FIXTURE/workspace"});
        restored.controller.setPersistenceContext(QStringLiteral("local-account"), true); QVERIFY(restored.readyController());
        QCOMPARE(restored.controller.operation(), AssistantOperation::Restoring);
        QVERIFY(!restored.controller.canSend());
        const QJsonObject user{{"type", "userMessage"}, {"id", "user-1"}, {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "first restored question"}}}}};
        auto persistedThread = restored.thread("thread-1", QJsonArray{
            turn("turn-1", "completed", QJsonArray{user, agent("agent-1", "restored answer")})});
        persistedThread.insert("environments", QJsonValue(QJsonValue::Null));
        restored.reply("thread/resume", restored.threadResult(persistedThread));
        QCOMPARE(restored.controller.operation(), AssistantOperation::Idle);
        QCOMPARE(restored.controller.messages().size(), 2);
        QCOMPARE(restored.controller.messages().front().source, QStringLiteral("first restored question"));
        QCOMPARE(restored.controller.messages().last().source, QStringLiteral("restored answer"));
        QVERIFY(restored.controller.canSend());

        MemoryConversationRepository incompatible = repository;
        incompatible.rows.front().binding.profileFingerprint += QStringLiteral("-old");
        Harness denied({}, {}, &incompatible); incompatible.rows.front().binding.codexHomeKey = QString::fromLatin1(
            QCryptographicHash::hash(denied.scratch.path().toUtf8(), QCryptographicHash::Sha256).toHex());
        denied.controller.setContext({"workspace-x", "Renamed workspace", "C:/fixture/workspace"});
        denied.controller.setPersistenceContext(QStringLiteral("local-account"), true); QVERIFY(denied.readyController());
        QVERIFY(denied.transport.lastRequest("thread/resume").isEmpty());
        QVERIFY(denied.controller.messages().isEmpty()); QVERIFY(!denied.controller.errorText().isEmpty());

        MemoryConversationRepository deleted = repository;
        Harness missing({}, {}, &deleted); deleted.rows.front().binding.codexHomeKey = QString::fromLatin1(
            QCryptographicHash::hash(missing.scratch.path().toUtf8(), QCryptographicHash::Sha256).toHex());
        missing.controller.setContext({"workspace-x", "Renamed workspace", "C:/fixture/workspace"});
        missing.controller.setPersistenceContext(QStringLiteral("local-account"), true); QVERIFY(missing.readyController());
        const auto resumeId = missing.requestId("thread/resume"); QVERIFY(resumeId > 0);
        missing.transport.deliver({{"id", resumeId}, {"error", QJsonObject{{"code", -32603}, {"message", "thread missing"}}}});
        QCOMPARE(missing.controller.operation(), AssistantOperation::Idle);
        QVERIFY(missing.controller.messages().isEmpty()); QCOMPARE(deleted.rows.size(), 1);
        QVERIFY(!missing.controller.canSend()); const auto missingConversationId = missing.controller.conversation().id;
        missing.controller.newChat(); QVERIFY(missing.controller.canSend());
        QVERIFY(missing.controller.conversation().id != missingConversationId); QCOMPARE(deleted.rows.size(), 1);

        MemoryConversationRepository oversized = repository;
        CodexClientLimits tinyHistory; tinyHistory.historyBytes = 64;
        Harness bounded({}, tinyHistory, &oversized); oversized.rows.front().binding.codexHomeKey = QString::fromLatin1(
            QCryptographicHash::hash(bounded.scratch.path().toUtf8(), QCryptographicHash::Sha256).toHex());
        bounded.controller.setContext({"workspace-x", "Renamed workspace", "C:/fixture/workspace"});
        bounded.controller.setPersistenceContext(QStringLiteral("local-account"), true); QVERIFY(bounded.readyController());
        const auto boundedResumeId = bounded.requestId("thread/resume"); QVERIFY(boundedResumeId > 0);
        bounded.reply("thread/resume", bounded.threadResult(bounded.thread("thread-1", QJsonArray{
            turn("turn-1", "completed", QJsonArray{user, agent("agent-1", "history larger than the cap")})})));
        QVERIFY(bounded.controller.messages().isEmpty());
        QVERIFY(!bounded.controller.errorText().isEmpty());
    }
    void savedConversationNeverAttachesToAnotherWorkspace()
    {
        MemoryConversationRepository repository;
        Harness original({}, {}, &repository);
        original.controller.setContext({"workspace-a", "A", "C:/workspace-a"});
        original.controller.setPersistenceContext("local-account", true);
        QVERIFY(original.readyController()); QVERIFY(original.sendAndAccept()); original.finish();
        QCOMPARE(repository.rows.size(), 1);
        Harness other({}, {}, &repository);
        repository.rows.front().binding.codexHomeKey = QString::fromLatin1(
            QCryptographicHash::hash(other.scratch.path().toUtf8(), QCryptographicHash::Sha256).toHex());
        other.controller.setContext({"workspace-b", "B", "C:/workspace-b"});
        other.controller.setPersistenceContext("local-account", true); QVERIFY(other.readyController());
        QVERIFY(other.transport.lastRequest("thread/resume").isEmpty());
        QVERIFY(other.controller.errorText().contains("different workspace"));
        QCOMPARE(repository.rows.front().conversation.context.workspaceId, QString("workspace-a"));
    }
    void partiallyAcceptedSendPersistsUncertaintyWithoutReplay()
    {
        MemoryConversationRepository repository; Harness h({}, {}, &repository);
        h.controller.setPersistenceContext(QStringLiteral("local-account"), true);
        QVERIFY(h.readyController()); QVERIFY(h.controller.send("may have been accepted")); h.reply("thread/start", h.threadResult());
        h.reply("turn/start", {{"turn", turn()}});
        QCOMPARE(repository.rows.size(), 1); QCOMPARE(repository.rows.front().lastTurnStatus, QStringLiteral("accepted"));
        const auto requests = h.transport.sent.size();
        emit h.transport.failed(AppError::failure(ErrorCode::CodexConnectionLost,
            ErrorCategory::Process, QStringLiteral("fixture disconnect"), {}, QStringLiteral("test")), h.transport.generation);
        QCOMPARE(h.controller.operation(), AssistantOperation::Uncertain);
        QCOMPARE(repository.rows.front().lastTurnStatus, QStringLiteral("uncertain"));
        QCOMPARE(h.transport.sent.size(), requests);
    }
    void nativeGateBeforeThreadCreation()
    {
        Harness h; h.transport.native = true; h.controller.open();
        QCOMPARE(h.controller.connection(), AssistantConnectionState::Failed);
        QVERIFY(!h.client.isInitialized()); QVERIFY(h.transport.launches.isEmpty());
        QCOMPARE(h.client.compatibilityReport().reason, CodexCompatibilityReason::EvidenceMissing);
        QVERIFY(!h.controller.canSend());
        QVERIFY(!h.controller.send("forbidden")); QVERIFY(h.transport.lastRequest("thread/start").isEmpty());
        QVERIFY(h.controller.statusText().contains("compatibility evidence"));
    }
    void featureInventoryFailureNamesTheFailedRestriction() {
        Harness h; QVERIFY(h.readyController());
        emit h.client.profileChecked(false, {"native-feature-enabled:shell_tool", "native-feature-enabled:hooks"});
        QVERIFY(h.controller.errorText().contains("command execution"));
        QVERIFY(h.controller.errorText().contains("Codex hooks"));
        QVERIFY(!h.controller.errorText().contains("native-feature-enabled"));
    }
    void synchronousTransportCompletion()
    {
        Harness h; QVERIFY(h.readyController()); QSignalSpy admitted(&h.controller, &AssistantController::sendAdmitted);
        h.transport.onSend = [&](const QJsonObject& request) {
            if (request.value("method") == "thread/start") h.transport.reply(request.value("id").toInteger(), h.threadResult());
            if (request.value("method") == "turn/start") h.transport.reply(request.value("id").toInteger(), {{"turn", turn("turn-1", "completed", QJsonArray{agent("agent-1", "inline answer")})}});
        };
        QVERIFY(h.controller.send("inline request")); QCOMPARE(admitted.size(), 1); QCOMPARE(h.controller.operation(), AssistantOperation::Idle);
        QCOMPARE(h.controller.messages().size(), 2); QCOMPARE(h.controller.messages().front().delivery, AssistantDelivery::Completed);
        QCOMPARE(h.controller.messages().last().source, QStringLiteral("inline answer"));
    }
    void rejectedAdmissionAndPreacceptRetry()
    {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.controller.send("keep draft"));
        h.transport.deliver({{"id", h.requestId("thread/start")}, {"error", QJsonObject{{"code", -32603}, {"message", "private error"}}}});
        QCOMPARE(h.controller.operation(), AssistantOperation::Idle); const auto id = h.controller.messages().front().id;
        QVERIFY(h.controller.canRetry(id)); QCOMPARE(h.controller.messages().front().delivery, AssistantDelivery::NotSent);
        QVERIFY(!h.controller.errorText().contains("private")); QSignalSpy admitted(&h.controller, &AssistantController::sendAdmitted);
        h.controller.retry(id); h.reply("thread/start", h.threadResult()); QCOMPARE(admitted.size(), 1);
        QCOMPARE(h.controller.messages().size(), 1); QCOMPARE(h.controller.messages().front().id, id);
    }
    void coalescesItemsAndReconcilesFinal()
    {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept()); QSignalSpy changed(&h.controller, &AssistantController::messageChanged);
        h.item("item/started", agent()); for (int i = 0; i < 10; ++i) h.delta("a"); QCOMPARE(changed.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 1000); QCOMPARE(h.controller.messages().last().source, QStringLiteral("aaaaaaaaaa"));
        h.item("item/completed", agent("agent-1", "canonical final")); QCOMPARE(changed.size(), 2);
        h.finish("completed", QJsonArray{agent("agent-1", "canonical final")});
        QCOMPARE(h.controller.messages().size(), 2); QCOMPARE(h.controller.messages().last().source, QStringLiteral("canonical final"));
        QVERIFY(h.controller.messages().last().final); QCOMPARE(h.controller.messages().last().delivery, AssistantDelivery::Completed); QVERIFY(h.controller.canSend());
        h.delta("late"); QCOMPARE(h.controller.messages().last().source, QStringLiteral("canonical final"));
    }
    void completionBeforeAcknowledgement()
    {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.controller.send("early")); h.reply("thread/start", h.threadResult());
        h.delta("before acknowledgement"); h.finish(); QCOMPARE(h.controller.messages().size(), 1);
        h.reply("turn/start", {{"turn", turn()}}); QCOMPARE(h.controller.messages().size(), 2);
        QCOMPARE(h.controller.messages().last().source, QStringLiteral("before acknowledgement"));
        QCOMPARE(h.controller.messages().last().delivery, AssistantDelivery::Completed); QVERIFY(h.controller.canSend());
    }
    void separateItemsAndHiddenReasoning()
    {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept());
        h.delta("one", "item-one"); h.delta("two", "item-two"); h.item("item/started", {{"type", "reasoning"}, {"id", "internal"}, {"summary", QJsonArray{}}, {"content", QJsonArray{}}});
        h.finish(); QCOMPARE(h.controller.messages().size(), 3); QCOMPARE(h.controller.messages().at(1).source, QStringLiteral("one"));
        QCOMPARE(h.controller.messages().at(2).source, QStringLiteral("two")); QVERIFY(h.controller.messages().at(1).id != h.controller.messages().at(2).id);
    }
    void caps_data()
    { QTest::addColumn<QString>("kind"); for (const auto* name : {"user", "assistant", "transcript", "cards"}) QTest::newRow(name) << QString::fromLatin1(name); }
    void caps()
    {
        QFETCH(QString, kind); AssistantLimits limits;
        if (kind == "user") limits.userBytes = 3;
        if (kind == "assistant") limits.assistantBytes = 5;
        if (kind == "transcript") limits.transcriptBytes = 6;
        if (kind == "cards") limits.messages = 2;
        Harness h(limits); QVERIFY(h.readyController());
        if (kind == "user") { QVERIFY(!h.controller.send("many bytes")); QVERIFY(h.controller.messages().isEmpty()); return; }
        QVERIFY(h.sendAndAccept("hi")); h.delta(kind == "cards" ? "one" : "large text");
        if (kind == "cards") h.delta("second", "second");
        QVERIFY(!h.controller.canSend()); QCOMPARE(h.controller.operation(), AssistantOperation::Cancelling);
        QVERIFY(!h.transport.lastRequest("turn/interrupt").isEmpty()); QVERIFY(!h.controller.errorText().isEmpty());
        if (kind != "cards") QVERIFY(h.controller.messages().last().truncated);
    }
    void stopFinalRaceAndNewChat()
    {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept()); const auto previous = h.controller.conversation().id;
        h.controller.newChat(); QCOMPARE(h.controller.conversation().id, previous); QCOMPARE(h.controller.operation(), AssistantOperation::Cancelling);
        h.finish(); QVERIFY(h.controller.conversation().id != previous); QVERIFY(h.controller.messages().isEmpty());
        QCOMPARE(h.controller.sessionConversations().size(), 1);
        QCOMPARE(h.controller.sessionConversations().front().conversation.id, previous);
        QCOMPARE(h.controller.sessionConversations().front().binding.threadId, QStringLiteral("thread-1"));
        h.reply("turn/interrupt", {}); QVERIFY(h.controller.canSend()); QVERIFY(h.controller.send("new conversation"));
        QCOMPARE(h.controller.operation(), AssistantOperation::CreatingThread);
    }
    void historyBeforeAcceptedRetry()
    {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept()); h.finish("interrupted");
        const auto id = h.controller.messages().front().id; QVERIFY(h.controller.canRetry(id)); const auto sent = h.transport.sent.size();
        h.controller.retry(id); QCOMPARE(h.controller.operation(), AssistantOperation::Reconciling);
        QCOMPARE(h.transport.sent.size(), sent + 1); QCOMPARE(h.transport.sent.last().value("method"), QJsonValue("thread/read"));
        h.reply("thread/read", {{"thread", h.thread("thread-1", QJsonArray{turn("turn-1", "interrupted")})}});
        QVERIFY(h.controller.retryIsNewTurn(id)); QCOMPARE(h.transport.sent.size(), sent + 1);
        h.controller.retry(id); QCOMPARE(h.transport.sent.last().value("method"), QJsonValue("turn/start"));
    }
    void crashNoAutomaticReplay()
    {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept()); const auto sent = h.transport.sent.size();
        emit h.transport.failed(devhub::domain::errors::AppError{}, h.transport.generation);
        QCOMPARE(h.controller.operation(), AssistantOperation::Uncertain); QCOMPARE(h.transport.sent.size(), sent);
        QCOMPARE(h.controller.messages().front().delivery, AssistantDelivery::Uncertain); QVERIFY(!h.controller.canSend());
        h.controller.retry(h.controller.messages().front().id); QCOMPARE(h.transport.sent.size(), sent);
    }
    void contextChangeAndSessionRevocation()
    {
        Harness h; h.controller.setContext({"workspace-a", "A", "C:/A"}); QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept());
        h.controller.setContext({"workspace-b", "B", "C:/B"}); QCOMPARE(h.controller.conversation().context.name, QStringLiteral("A"));
        QCOMPARE(h.controller.operation(), AssistantOperation::Cancelling); h.finish("interrupted"); QVERIFY(!h.controller.canSend());
        h.controller.newChat(); QCOMPARE(h.controller.conversation().context.name, QStringLiteral("B"));
        h.controller.setSessionAllowed(false, true); QVERIFY(h.controller.messages().isEmpty()); QVERIFY(!h.controller.canSend());
        QVERIFY(h.controller.sessionConversations().isEmpty());
    }
    void shutdownInterruptsBeforeStoppingAndRejectsLateWork()
    {
        MemoryConversationRepository repository; Harness h({}, {}, &repository);
        h.controller.setPersistenceContext(QStringLiteral("local-account"), true);
        QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept(QStringLiteral("shutdown marker")));
        QSignalSpy revoked(&h.controller, &AssistantController::accessRevoked);
        QSignalSpy stopped(&h.controller, &AssistantController::shutdownFinished);
        bool completed = false;
        h.controller.beginShutdown([&completed] { completed = true; });
        QCOMPARE(revoked.size(), 1); QVERIFY(!h.controller.canSend());
        QCOMPARE(h.transport.stops, 0);
        QCOMPARE(h.transport.lastRequest(QStringLiteral("turn/interrupt")).value("method"), QJsonValue("turn/interrupt"));
        h.finish(QStringLiteral("interrupted"));
        QVERIFY(completed); QCOMPARE(stopped.size(), 1); QCOMPARE(h.transport.stops, 1);
        QVERIFY(!h.controller.canSend());
        QCOMPARE(repository.rows.back().lastTurnStatus, QStringLiteral("interrupted"));
        const auto messages = h.controller.messages().size();
        h.delta(QStringLiteral("late callback")); QCOMPARE(h.controller.messages().size(), messages);
    }
    void lockRevokesAssistantWithoutDeletingCodexAssociation()
    {
        MemoryConversationRepository repository; Harness h({}, {}, &repository);
        h.controller.setPersistenceContext(QStringLiteral("local-account"), true);
        QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept());
        const auto access = h.controller.accessGeneration();
        h.controller.setSessionAllowed(false, true);
        QVERIFY(h.controller.accessGeneration() > access); QVERIFY(h.controller.messages().isEmpty());
        QVERIFY(!h.controller.canSend()); QCOMPARE(repository.rows.size(), 1);
        QCOMPARE(h.transport.stops, 0);
        h.finish(QStringLiteral("interrupted"));
        QCOMPARE(h.transport.stops, 1); QCOMPARE(repository.rows.size(), 1);
        h.controller.setSessionAllowed(true); QVERIFY(h.controller.setupAllowed());
    }
    void cancellationWatchdog()
    {
        AssistantLimits limits; limits.cancellationMilliseconds = 30; Harness h(limits); QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept());
        h.controller.stopTurn(); QTRY_COMPARE_WITH_TIMEOUT(h.transport.stops, 1, 1000); QCOMPARE(h.controller.operation(), AssistantOperation::Uncertain);
    }
    void sessionOnlyHistoryAndStaleProbe()
    {
        Harness h; const auto result = h.controller.savedConversations("devhub-account"); QVERIFY(!result); QCOMPARE(result.error().code, ErrorCode::AssistantHistoryUnavailable);
        QVERIFY(h.readyController()); const auto sent = h.transport.sent.size();
        h.controller.executableFound({"invalid", "0.160.1"}, 999); QCOMPARE(h.transport.sent.size(), sent);
    }
};
QTEST_GUILESS_MAIN(AssistantControllerTest)
#include "assistant_controller_test.moc"
