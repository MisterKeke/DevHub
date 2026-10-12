#include "support/AssistantHarness.h"
#include "presentation/assistant/AssistantPage.h"
#include "application/assistant/AssistantApprovalCoordinator.h"
#include "application/assistant/AssistantToolRuntime.h"
#include "infrastructure/codex/CodexProcess.h"
#include "infrastructure/codex/CodexExecutableLocator.h"
#include "presentation/assistant/AssistantNavigation.h"
#include "presentation/widgets/SafeMarkdownBrowser.h"
#include "presentation/widgets/CurrentPageStack.h"
#include "presentation/theme/ThemeManager.h"
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSettings>
#include <QTextCursor>
#include <QTest>
using namespace devhub::application::assistant;
using namespace devhub::tests::assistant;
using namespace devhub::tests::codex;
using devhub::presentation::assistant::AssistantPage;
using devhub::presentation::widgets::SafeMarkdownBrowser;
class AssistantUiTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_settingsRoot;
    std::unique_ptr<devhub::presentation::theme::ThemeManager> m_theme;
private slots:
    void reconnectReplacesUncertainMessageCardsWithoutDuplicates() {
        Harness h({}, {}, nullptr, "0.162.0-alpha.17.2"); QVERIFY(h.readyController());
        AssistantPage page(h.controller); page.resize(850, 520); page.show();
        QVERIFY(h.sendAndAccept("question")); h.delta("answer");
        auto invalid = turn("turn-1", "completed"); invalid.insert("itemsView", "unknown");
        h.transport.event("turn/completed", {{"threadId", "thread-1"}, {"turn", invalid}});
        QCOMPARE(page.findChildren<SafeMarkdownBrowser*>().size(), 2);
        page.findChild<QPushButton*>("assistantReconnect")->click(); QVERIFY(h.readyController());
        const QJsonObject user{{"type", "userMessage"}, {"id", "user-1"}, {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "question"}}}}};
        h.reply("thread/resume", h.threadResult(h.thread("thread-1", QJsonArray{turn("turn-1", "completed", QJsonArray{user, agent("agent-1", "answer")})})));
        QCOMPARE(page.findChildren<SafeMarkdownBrowser*>().size(), 2);
        page.findChild<QPlainTextEdit*>("assistantComposer")->setPlainText("next question");
        QVERIFY(page.findChild<QPushButton*>("assistantSend")->isEnabled());
        QVERIFY(!page.findChild<QLabel*>("assistantStatus")->text().contains("uncertain"));
        for (const auto& message : h.controller.messages()) QCOMPARE(message.delivery, AssistantDelivery::Completed);
    }
    void installedNativeReconnectWithVisibleChat() {
        const auto executable = qEnvironmentVariable("DEVHUB_READONLY_CODEX_EXE");
        if (executable.isEmpty()) QSKIP("Opt-in read-only native account/profile/catalog check; no prompt or login.");
        QTemporaryDir scratch; QVERIFY(scratch.isValid());
        devhub::infrastructure::codex::CodexProcess process;
        devhub::infrastructure::codex::CodexClient client(process);
        devhub::infrastructure::codex::CodexExecutableLocator locator;
        FixedClock clock; SequenceIds ids;
        QVERIFY(client.configureDynamicTools(devhub::application::assistant::AssistantToolRuntime::experimentalToolSpecifications()));
        AssistantController controller(client, clock, ids, scratch.path(),
            devhub::infrastructure::codex::CodexRuntimeProfile::launchArguments().value(),
            codexBaseEnvironment(QProcessEnvironment::systemEnvironment()));
        controller.applyConfiguration({executable, {}});
        QObject::connect(&controller, &AssistantController::executableProbeRequested, &locator, &devhub::infrastructure::codex::CodexExecutableLocator::locate);
        QObject::connect(&controller, &AssistantController::cancelProbeRequested, &locator, &devhub::infrastructure::codex::CodexExecutableLocator::cancel);
        QObject::connect(&locator, &devhub::infrastructure::codex::CodexExecutableLocator::executableFound, &controller, &AssistantController::executableFound);
        QObject::connect(&locator, &devhub::infrastructure::codex::CodexExecutableLocator::probeFailed, &controller, &AssistantController::probeFailed);
        AssistantApprovalCoordinator approvals; controller.setApprovalCoordinator(&approvals);
        AssistantPage page(controller); page.resize(850, 520); page.show();
        auto* reconnect = page.findChild<QPushButton*>("assistantReconnect");
        int startingReconnects = 0;
        bool startingReconnectQueued = false;
        QObject::connect(&controller, &AssistantController::stateChanged, &page, [&] {
            if (controller.connection() != AssistantConnectionState::Starting || startingReconnects >= 3 || startingReconnectQueued) return;
            startingReconnectQueued = true;
            QTimer::singleShot(0, &page, [&] {
                startingReconnectQueued = false;
                if (controller.connection() != AssistantConnectionState::Starting) return;
                ++startingReconnects; reconnect->click();
            });
        });
        page.activate();
        auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer"); composer->setPlainText("Unsent draft");
        for (int attempt = 0; attempt < 4; ++attempt) {
            QTRY_VERIFY_WITH_TIMEOUT(controller.canSend(), 15000);
            QCOMPARE(composer->toPlainText(), QStringLiteral("Unsent draft"));
            QVERIFY(page.findChild<QPushButton*>("assistantSend")->isEnabled());
            if (attempt != 3) reconnect->click();
        }
        QCOMPARE(startingReconnects, 3);
        bool stopped = false; controller.beginShutdown([&] { stopped = true; });
        QTRY_VERIFY_WITH_TIMEOUT(stopped, 10000);
    }
    void nativeAlphaReadyEnablesSend() {
        Harness h({}, {}, nullptr, "0.162.0-alpha.17.2"); h.transport.native = true; QVERIFY(h.readyController());
        AssistantPage page(h.controller); page.resize(850, 520); page.show();
        auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer");
        auto* send = page.findChild<QPushButton*>("assistantSend"); QVERIFY(!send->isEnabled());
        composer->setPlainText("User chose to send this text"); QVERIFY(send->isEnabled());
        send->click(); QVERIFY(!h.transport.lastRequest("thread/start").isEmpty());
        h.reply("thread/start", h.threadResult()); QVERIFY(!h.transport.lastRequest("turn/start").isEmpty());
        QVERIFY(composer->toPlainText().isEmpty()); QVERIFY(!send->isEnabled());
        QVERIFY(!h.controller.notesDisclosureGranted()); QVERIFY(!h.controller.filesDisclosureGranted());
    }
    void hiddenModulesDoNotPushComposerOffscreen() {
        Harness h; h.controller.open();
        h.reply("initialize", {{"codexHome", h.scratch.path()}, {"userAgent", "fixture"}, {"platformFamily", "windows"}, {"platformOs", "windows"}});
        h.reply("account/read", chatGptAccount());
        auto result = safeConfig(); auto config = result.value("config").toObject();
        config.insert("notify", QJsonArray{"private-command"});
        config.insert("mcp_servers", QJsonObject{{"private-server", QJsonObject{{"enabled", true}}}});
        config.insert("desktop", QJsonObject{{"private", true}}); result.insert("config", config);
        h.reply("config/read", result); h.reply("configRequirements/read", {{"requirements", QJsonValue(QJsonValue::Null)}});
        QDialog window; auto* layout = new QVBoxLayout(&window); layout->setContentsMargins(0, 0, 0, 0);
        auto* stack = new devhub::presentation::widgets::CurrentPageStack(&window); layout->addWidget(stack);
        auto* tallModule = new QWidget; tallModule->setMinimumSize(1000, 1800); stack->addWidget(tallModule);
        auto* page = new AssistantPage(h.controller); stack->addWidget(page); stack->setCurrentWidget(page);
        for (const int width : {720, 850, 1120}) {
            window.resize(width, 520); window.show(); QTest::qWait(20);
            QCOMPARE(window.height(), 520);
            for (const auto& name : {"assistantComposer", "assistantSend", "assistantStop", "assistantReconnect"}) {
                auto* control = page->findChild<QWidget*>(name); QVERIFY(control);
                QVERIFY(window.rect().contains(QRect(control->mapTo(&window, QPoint{}), control->size())));
            }
            const auto snapshot = qEnvironmentVariable("DEVHUB_ASSISTANT_LAYOUT_SNAPSHOT");
            if (width == 850 && !snapshot.isEmpty()) QVERIFY(window.grab().save(snapshot));
        }
        stack->setCurrentWidget(tallModule); QVERIFY(stack->minimumSizeHint().height() >= 1800);
        stack->setCurrentWidget(page); QVERIFY(stack->minimumSizeHint().height() < 520);
    }
    void pendingPreviewsScrollWithoutDisplacingComposer() {
        Harness h; AssistantApprovalCoordinator approvals; h.controller.setApprovalCoordinator(&approvals);
        AssistantPage page(h.controller); page.resize(850, 520); page.show();
        for (int i = 0; i < 2; ++i) {
            ToolCall call; call.callId = QString::number(i); call.name = "devhub_notes_edit"; call.expiresAtMonotonicMs = 100000;
            (void)approvals.request(call, {}, "Workspace", "Replace note", QString(8000, 'x'), [](bool) {});
        }
        QTest::qWait(20); QCOMPARE(page.height(), 520);
        auto* scroll = page.findChild<QScrollArea*>("assistantApprovals"); QVERIFY(scroll->isVisible());
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        for (const auto& name : {"assistantComposer", "assistantSend", "assistantStop"}) {
            auto* control = page.findChild<QWidget*>(name); QVERIFY(control);
            QVERIFY(page.rect().contains(QRect(control->mapTo(&page, QPoint{}), control->size())));
        }
        page.hide(); QVERIFY(approvals.pending().isEmpty()); h.controller.setApprovalCoordinator(nullptr);
    }
    void approvalsAreExplicitAndHidingDenies() {
        Harness h; AssistantApprovalCoordinator approvals; h.controller.setApprovalCoordinator(&approvals);
        AssistantPage page(h.controller); page.resize(850, 650); page.show();
        ToolCall call; call.callId = "approved-call"; call.name = "devhub_notes_edit";
        call.expiresAtMonotonicMs = 100000;
        int decisions = 0; bool allowed = false;
        auto ask = [&] { return approvals.request(call, {}, "Workspace", "Replace note", "<script>PRIVATE_PROPOSED_TEXT</script>", [&](bool allow) { ++decisions; allowed = allow; }); };
        const auto first = ask(); QCOMPARE(decisions, 0);
        QCOMPARE(page.findChild<QLabel*>("assistantStatus")->text(), QString("Waiting for your approval."));
        auto* preview = page.findChild<QPlainTextEdit*>("assistantApprovalPreview"); QVERIFY(preview && preview->isReadOnly());
        QCOMPARE(preview->toPlainText(), QString("<script>PRIVATE_PROPOSED_TEXT</script>"));
        auto* allow = page.findChild<QPushButton*>("assistantApprovalAllow"); QVERIFY(allow); allow->click();
        QCOMPARE(decisions, 1); QVERIFY(allowed); QVERIFY(!approvals.decide(first, true));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        ask(); page.hide(); QCOMPARE(decisions, 2); QVERIFY(!allowed); QVERIFY(approvals.pending().isEmpty());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        page.show(); ask();
        page.findChild<QPushButton*>("assistantApprovalDeny")->click(); QCOMPARE(decisions, 3); QVERIFY(!allowed);
        h.controller.setApprovalCoordinator(nullptr);
    }
    void initTestCase()
    {
        // The offscreen plugin has no Windows font database integration.
        // Load a system font for meaningful layout/DPI assertions and snapshots.
        const int font = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QVERIFY(font >= 0); QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
        QCoreApplication::setOrganizationName(QStringLiteral("DevHubAssistantTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsRoot.path());
        m_theme = std::make_unique<devhub::presentation::theme::ThemeManager>(*qApp);
    }
    void draftUntilTurnAdmission()
    {
        Harness h; QVERIFY(h.readyController()); AssistantPage page(h.controller); page.resize(850, 650); page.show();
        auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer"); composer->setPlainText("retain draft");
        QTest::keyClick(composer, Qt::Key_Return); QCOMPARE(composer->toPlainText(), QStringLiteral("retain draft"));
        h.reply("thread/start", h.threadResult()); QVERIFY(composer->toPlainText().isEmpty());
        QVERIFY(!page.findChild<QPushButton*>("assistantSend")->isEnabled());
    }
    void threadFailureKeepsDraft()
    {
        Harness h; QVERIFY(h.readyController()); AssistantPage page(h.controller); auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer");
        composer->setPlainText("not lost"); page.findChild<QPushButton*>("assistantSend")->click();
        h.transport.deliver({{"id", h.requestId("thread/start")}, {"error", QJsonObject{{"code", -32603}, {"message", "private"}}}});
        QCOMPARE(composer->toPlainText(), QStringLiteral("not lost")); QVERIFY(!page.findChild<QLabel*>("assistantError")->text().isEmpty());
    }
    void editingDuringAdmissionPreservesNewDraft()
    {
        Harness h; QVERIFY(h.readyController()); AssistantPage page(h.controller); auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer");
        composer->setPlainText("original"); page.findChild<QPushButton*>("assistantSend")->click(); composer->setPlainText("next draft");
        h.reply("thread/start", h.threadResult()); QCOMPARE(composer->toPlainText(), QStringLiteral("next draft"));
    }
    void lockedSessionDisablesActionsAndPreservesDraft()
    {
        Harness h; QVERIFY(h.readyController()); AssistantPage page(h.controller); auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer");
        composer->setPlainText("private draft"); h.controller.setSessionAllowed(false);
        for (const auto& name : {"assistantSend", "assistantStop", "assistantNewChat", "assistantReconnect"}) QVERIFY(!page.findChild<QPushButton*>(name)->isEnabled());
        QCOMPARE(composer->toPlainText(), QStringLiteral("private draft"));
    }
    void readDisclosureControlsAndToolActivityAreExplicit()
    {
        Harness h; QVERIFY(h.readyController()); AssistantPage page(h.controller);
        auto* notes = page.findChild<QCheckBox*>(QStringLiteral("assistantAllowNoteText"));
        auto* files = page.findChild<QCheckBox*>(QStringLiteral("assistantAllowFileText"));
        auto* activity = page.findChild<QLabel*>(QStringLiteral("assistantToolActivity"));
        QVERIFY(notes && files && activity);
        QVERIFY(!notes->isChecked() && !files->isChecked());
        QVERIFY(!h.controller.notesDisclosureGranted() && !h.controller.filesDisclosureGranted());
        h.controller.setContext({QStringLiteral("workspace-id"), QStringLiteral("Workspace"), QStringLiteral("C:/Workspace")});
        QVERIFY(notes->isEnabled() && files->isEnabled());
        notes->click(); QVERIFY(h.controller.notesDisclosureGranted()); QVERIFY(!h.controller.filesDisclosureGranted());
        files->click(); QVERIFY(h.controller.notesDisclosureGranted() && h.controller.filesDisclosureGranted());
        h.controller.reportToolActivity(QStringLiteral("files.read"), true);
        QVERIFY(activity->text().contains(QStringLiteral("files.read")));
        h.controller.reportToolActivity(QStringLiteral("files.read"), false, true);
        QVERIFY(activity->text().contains(QStringLiteral("success")));
        h.controller.setSessionAllowed(false);
        QVERIFY(!notes->isChecked() && !files->isChecked());
        QVERIFY(!h.controller.notesDisclosureGranted() && !h.controller.filesDisclosureGranted());
    }
    void shiftEnterAndIme()
    {
        Harness h; QVERIFY(h.readyController()); AssistantPage page(h.controller); page.show(); auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer");
        composer->setPlainText("draft"); composer->moveCursor(QTextCursor::End); QTest::keyClick(composer, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(composer->toPlainText(), QStringLiteral("draft\n")); QVERIFY(h.controller.messages().isEmpty());
        QInputMethodEvent preedit(QStringLiteral("候"), {}); QApplication::sendEvent(composer, &preedit); QTest::keyClick(composer, Qt::Key_Return);
        QVERIFY(h.controller.messages().isEmpty()); QInputMethodEvent committed; committed.setCommitString(QStringLiteral("候")); QApplication::sendEvent(composer, &committed);
        QTest::keyClick(composer, Qt::Key_Return); QCOMPARE(h.controller.messages().size(), 1);
    }
    void keyboardFocusAndActions()
    {
        Harness h; QVERIFY(h.readyController()); AssistantPage page(h.controller); page.resize(900, 700); page.show(); page.activateWindow();
        auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer");
        auto* send = page.findChild<QPushButton*>("assistantSend");
        auto* stop = page.findChild<QPushButton*>("assistantStop");
        auto* newChat = page.findChild<QPushButton*>("assistantNewChat");
        auto* reconnect = page.findChild<QPushButton*>("assistantReconnect");
        composer->setPlainText("Keyboard message"); composer->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), composer);
        QTest::keyClick(composer, Qt::Key_Tab); QCOMPARE(QApplication::focusWidget(), send);
        QTest::keyClick(send, Qt::Key_Tab); QCOMPARE(QApplication::focusWidget(), newChat);
        QTest::keyClick(newChat, Qt::Key_Tab); QCOMPARE(QApplication::focusWidget(), reconnect);
        QTest::keyClick(reconnect, Qt::Key_Tab, Qt::ShiftModifier); QCOMPARE(QApplication::focusWidget(), newChat);
        composer->setFocus();
        QKeyEvent repeated(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString{}, true);
        QApplication::sendEvent(composer, &repeated); QVERIFY(h.controller.messages().isEmpty());
        QTest::keyClick(composer, Qt::Key_Return); QCOMPARE(h.controller.messages().size(), 1);
        h.reply("thread/start", h.threadResult()); h.reply("turn/start", {{"turn", turn()}});
        QVERIFY(stop->isEnabled()); QVERIFY(!send->isEnabled()); stop->setFocus();
        QTest::keyClick(stop, Qt::Key_Space); QCOMPARE(h.controller.operation(), AssistantOperation::Cancelling);
        QVERIFY(h.requestId("turn/interrupt") > 0); h.finish("interrupted");
        newChat->setFocus(); QTest::keyClick(newChat, Qt::Key_Space); QVERIFY(h.controller.messages().isEmpty());
        for (auto* button : {send, stop, newChat, reconnect}) QVERIFY(!button->accessibleName().isEmpty());
        QVERIFY(!composer->accessibleName().isEmpty());
    }
    void markdownResourceAndLinkPolicy()
    {
        SafeMarkdownBrowser browser; browser.resize(550, 350);
        browser.setSourceText("# Heading\n\n**bold** and `code`\n\n<script>literal</script>\n\n![remote](https://example.test/image.png)\n![file](file:///private.png)\n![qrc](qrc:/branding/devhub.svg)\n\n```cpp\n<b>code stays literal</b>\n```");
        QVERIFY(!browser.openLinks()); QVERIFY(!browser.openExternalLinks()); QVERIFY(browser.toPlainText().contains("<script>literal</script>"));
        QVERIFY(browser.toPlainText().contains("<b>code stays literal</b>"));
        for (const auto& url : {"https://example.test/image.png", "file:///private.png", "qrc:/branding/devhub.svg", "data:image/png;base64,AAA"})
            QVERIFY2(browser.document()->resource(QTextDocument::ImageResource, QUrl(url)).isNull(), url);
        QVERIFY(SafeMarkdownBrowser::acceptsLink(QUrl("https://example.test/page")));
        for (const auto& url : {"javascript:alert(1)", "data:text/plain,text", "file:///private", "qrc:/branding/devhub.svg", "https://user:pass@example.test"}) QVERIFY(!SafeMarkdownBrowser::acceptsLink(QUrl(url)));
        SafeMarkdownBrowser notes(nullptr, SafeMarkdownBrowser::ResourcePolicy::NotesQrc); notes.setSourceText("[link](https://example.test)\n**Notes**");
        QVERIFY(!notes.openLinks()); QVERIFY(!notes.openExternalLinks()); QVERIFY(notes.toPlainText().contains("Notes"));
    }
    void selectionSurvivesUpdateAndPlainUserText()
    {
        SafeMarkdownBrowser browser; browser.setSourceText("stable prefix"); auto cursor = browser.textCursor(); cursor.setPosition(0); cursor.setPosition(6, QTextCursor::KeepAnchor); browser.setTextCursor(cursor);
        browser.setSourceText("stable prefix with streaming text"); QCOMPARE(browser.textCursor().selectedText(), QStringLiteral("stable"));
        browser.setSourceText("<b>plain user</b>", false); QCOMPARE(browser.toPlainText(), QStringLiteral("<b>plain user</b>"));
    }
    void streamedPageCopyScrollAndSnapshot()
    {
        Harness h; QVERIFY(h.readyController()); AssistantPage page(h.controller); page.resize(900, 700); page.show();
        QVERIFY(h.sendAndAccept("Explain **Markdown** <b>plain</b>"));
        QString answer = "# A streamed answer\n\n- First point\n- Second point\n\n```cpp\nQString message = \"Hello\";\n```\n\n";
        answer += QString("A longer paragraph for scroll testing.\n\n").repeated(35);
        h.item("item/completed", agent("agent-1", answer)); QTest::qWait(60);
        auto* scroll = page.findChild<QScrollArea*>("assistantTranscript"); QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QCOMPARE(scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
        QCOMPARE(scroll->widget()->y(), -scroll->verticalScrollBar()->value());
        scroll->verticalScrollBar()->setValue(0); auto* body = page.findChild<SafeMarkdownBrowser*>(h.controller.messages().last().id.toString(QUuid::WithoutBraces)); QVERIFY(body);
        auto cursor = body->textCursor(); cursor.setPosition(2); cursor.setPosition(10, QTextCursor::KeepAnchor); body->setTextCursor(cursor);
        h.finish(); QTest::qWait(30); QCOMPARE(scroll->verticalScrollBar()->value(), 0); QVERIFY(page.findChild<QPushButton*>("assistantLatest")->isVisible());
        QCOMPARE(body->textCursor().selectedText().size(), 8);
        QPushButton* copy = nullptr; for (auto* button : body->parentWidget()->findChildren<QPushButton*>()) if (button->text() == "Copy") copy = button;
        QVERIFY(copy); copy->click(); QCOMPARE(QApplication::clipboard()->text(), answer);
        page.findChild<QPushButton*>("assistantLatest")->click(); QCOMPARE(scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
        QCOMPARE(scroll->widget()->y(), -scroll->verticalScrollBar()->value());
    }
    void visualSample()
    {
        Harness h; h.controller.setContext({"workspace", "Example workspace", "C:/Example"}); QVERIFY(h.readyController());
        AssistantPage page(h.controller); page.resize(900, 700); page.show(); QVERIFY(h.sendAndAccept("How does streaming work in DevHub?"));
        const QString answer = "## Streaming inside DevHub\n\nMessages arrive as small text updates. The page refreshes changed cards every **50 ms**.\n\n"
            "- **Stop** interrupts the current turn.\n- **Copy** uses the original message source.\n- **New Chat** keeps earlier Codex history.\n\n"
            "```cpp\nQString message = \"Hello from DevHub\";\n```\n\nRaw tags such as <b>this</b> stay literal.";
        h.item("item/completed", agent("agent-1", answer)); h.finish(); QTest::qWait(50);
        QVERIFY(page.findChild<QPlainTextEdit*>("assistantComposer")->height() >= 85);
        QVERIFY(page.findChild<QPushButton*>("assistantNewChat")->isVisible());
        for (const auto& name : {"assistantComposer", "assistantSend", "assistantStop", "assistantNewChat", "assistantReconnect"}) {
            auto* control = page.findChild<QWidget*>(name); QVERIFY(control);
            QVERIFY(page.rect().contains(QRect(control->mapTo(&page, QPoint{}), control->size())));
            if (auto* button = qobject_cast<QPushButton*>(control)) QVERIFY(button->width() >= button->fontMetrics().horizontalAdvance(button->text()));
        }
        const auto snapshot = qEnvironmentVariable("DEVHUB_ASSISTANT_SNAPSHOT"); if (!snapshot.isEmpty()) QVERIFY(page.grab().save(snapshot));
    }
    void inputCapAndDisconnectedState()
    {
        Harness h; AssistantPage page(h.controller); auto* composer = page.findChild<QPlainTextEdit*>("assistantComposer"); composer->setPlainText("draft");
        QVERIFY(!page.findChild<QPushButton*>("assistantSend")->isEnabled()); QVERIFY(h.readyController());
        composer->setPlainText(QString(65537, QChar('a'))); QVERIFY(!page.findChild<QPushButton*>("assistantSend")->isEnabled());
        QVERIFY(page.findChild<QLabel*>("assistantError")->text().contains("64 KiB"));
    }
    void navigationAndSessionOnlyRecents()
    {
        devhub::application::commands::CommandRegistry registry; int selected = -1;
        devhub::presentation::assistant::AssistantNavigation::registerCommand(registry, [&](int index) { selected = index; });
        QVERIFY(registry.contains("nav.assistant")); QVERIFY(!registry.contains("nav.quickAsk"));
        QVERIFY(registry.descriptor("nav.assistant").availability.available); QVERIFY(registry.execute("nav.assistant")); QCOMPARE(selected, 9);
        QCOMPARE(registry.recentCommandIds(), QStringList{"nav.assistant"});
        selected = -1; QVERIFY(registry.execute("nav.assistant")); QCOMPARE(selected, 9);
        devhub::application::commands::CommandRegistry nextSession; QVERIFY(nextSession.recentCommandIds().isEmpty());
    }
};
QTEST_MAIN(AssistantUiTest)
#include "assistant_ui_test.moc"
