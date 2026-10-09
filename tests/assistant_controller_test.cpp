#include "support/AssistantHarness.h"
#include <QSignalSpy>
#include <QTest>
using namespace devhub::application::assistant;
using namespace devhub::tests::assistant;
using namespace devhub::tests::codex;
using devhub::domain::errors::ErrorCode;
class AssistantControllerTest final : public QObject {
    Q_OBJECT
private slots:
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
    void nativeGateBeforeThreadCreation()
    {
        Harness h; h.transport.native = true; QVERIFY(h.readyController()); QVERIFY(!h.controller.canSend());
        QVERIFY(!h.controller.send("forbidden")); QVERIFY(h.transport.lastRequest("thread/start").isEmpty());
        QVERIFY(h.controller.statusText().contains("verification"));
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
