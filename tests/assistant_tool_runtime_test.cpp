#include "application/assistant/AssistantToolRuntime.h"
#include "application/assistant/AssistantController.h"
#include "application/git/GitExecutableLocator.h"
#include "application/git/GitService.h"
#include "support/AssistantHarness.h"
#include "support/SyntheticSomethingProfile.h"
#include "application/services/ServiceCoordinator.h"
#include "application/workspaces/IWorkspaceRepository.h"
#include "application/workspaces/WorkspaceValidation.h"
#include "shared/identifiers/IdGenerator.h"
#include "shared/time/ApplicationClock.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QCryptographicHash>

using namespace devhub;
using namespace devhub::application;
using namespace devhub::application::assistant;

namespace {
domain::errors::AppError failure()
{
    return domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument,
        domain::errors::ErrorCategory::Internal, QStringLiteral("Not used in this test."), {}, QStringLiteral("test"));
}

class FakeToolClient final : public ICodexClient
{
public:
    using ICodexClient::ICodexClient;
    shared::common::Result<void> launch(const CodexLaunchSpec& spec) override
    { generation = spec.processGeneration; return shared::common::Result<void>::success(); }
    void stop() override {}
    shared::common::Result<qint64> readAccount(bool = false) override { return shared::common::Result<qint64>::failure(failure()); }
    shared::common::Result<qint64> startLogin(CodexLoginMode) override { return shared::common::Result<qint64>::failure(failure()); }
    shared::common::Result<qint64> cancelLogin() override { return shared::common::Result<qint64>::failure(failure()); }
    shared::common::Result<qint64> listModels(const QString& = {}) override { return shared::common::Result<qint64>::failure(failure()); }
    shared::common::Result<qint64> startThread(const QString&) override { return shared::common::Result<qint64>::failure(failure()); }
    shared::common::Result<qint64> resumeThread(const CodexThreadBinding&) override { return shared::common::Result<qint64>::failure(failure()); }
    shared::common::Result<qint64> readThread(const CodexThreadBinding&) override { return shared::common::Result<qint64>::failure(failure()); }
    shared::common::Result<qint64> startTurn(const QString&) override { return shared::common::Result<qint64>::failure(failure()); }
    shared::common::Result<qint64> interruptTurn() override { return shared::common::Result<qint64>::failure(failure()); }
    bool isInitialized() const override { return initialized; }
    bool profileVerified() const override { return verified; }
    bool canStartConversation() const override { return true; }
    bool canSend() const override { return true; }
    CodexAccount account() const override { return accountValue; }
    CodexThreadBinding threadBinding() const override { return binding; }
    QString activeTurnId() const override { return turnId; }
    quint64 processGeneration() const override { return generation; }
    bool configureDynamicTools(QJsonArray value) override { specifications = std::move(value); return true; }
    bool resolveDynamicToolCall(const QJsonValue& id, const QJsonObject& response) override
    { replies.append({id, response}); return true; }
    void call(QJsonValue id, const QString& tool, QJsonObject arguments = {})
    {
        emit dynamicToolCallReceived(id, {{QStringLiteral("threadId"), binding.threadId},
            {QStringLiteral("turnId"), turnId}, {QStringLiteral("callId"), QString::fromLatin1(id.toVariant().toString().toUtf8().toHex())},
            {QStringLiteral("tool"), tool}, {QStringLiteral("arguments"), arguments}}, generation);
    }
    bool initialized = true;
    bool verified = true;
    quint64 generation = 0;
    CodexAccount accountValue = [] {
        CodexAccount value; value.mode = CodexAccountMode::ChatGpt; value.requiresOpenaiAuth = true;
        value.identityFingerprint = QStringLiteral("account-fingerprint"); return value;
    }();
    CodexThreadBinding binding{QStringLiteral("thread-1"), QStringLiteral("account-fingerprint"), QStringLiteral("home"), {}, QStringLiteral("scratch"), QStringLiteral("profile")};
    QString turnId = QStringLiteral("turn-1");
    QJsonArray specifications;
    QList<QPair<QJsonValue, QJsonObject>> replies;
};

class WorkspaceRepo final : public workspaces::IWorkspaceRepository
{
public:
    shared::common::Result<QVector<domain::workspace::Workspace>> listForAccount(const domain::account::AccountId&) override
    { ++listCalls; return shared::common::Result<QVector<domain::workspace::Workspace>>::success(rows); }
    shared::common::Result<QVector<domain::workspace::Workspace>> listArchivedForAccount(const domain::account::AccountId&) override
    { return shared::common::Result<QVector<domain::workspace::Workspace>>::success({}); }
    shared::common::Result<domain::workspace::Workspace> add(const domain::workspace::Workspace&) override { return shared::common::Result<domain::workspace::Workspace>::failure(failure()); }
    shared::common::Result<void> rename(const domain::workspace::WorkspaceId&, const domain::account::AccountId&, const QString&) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> setFavorite(const domain::workspace::WorkspaceId&, const domain::account::AccountId&, bool) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> updateLocation(const domain::workspace::WorkspaceId&, const domain::account::AccountId&, const domain::workspace::WorkspacePath&) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> archive(const domain::workspace::WorkspaceId&, const domain::account::AccountId&) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> restore(const domain::workspace::WorkspaceId&, const domain::account::AccountId&) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> recordOpened(const domain::workspace::WorkspaceId&, const domain::account::AccountId&, const QDateTime&) override { ++opened; return shared::common::Result<void>::success(); }
    shared::common::Result<void> setRecentOrder(const domain::account::AccountId&, const QVector<domain::workspace::WorkspaceId>&) override { return shared::common::Result<void>::success(); }
    QVector<domain::workspace::Workspace> rows;
    int listCalls = 0;
    int opened = 0;
};

class ServiceRepo final : public services::IServiceDefinitionRepository
{
public:
    shared::common::Result<QVector<domain::services::ServiceDefinition>> list(const QString& id) override
    { ++listCalls; lastWorkspace = id; return shared::common::Result<QVector<domain::services::ServiceDefinition>>::success(rows); }
    shared::common::Result<void> save(const domain::services::ServiceDefinition&) override { ++saves; return shared::common::Result<void>::success(); }
    shared::common::Result<void> remove(const QString&, const QString&) override { return shared::common::Result<void>::success(); }
    QVector<domain::services::ServiceDefinition> rows;
    int listCalls = 0;
    QString lastWorkspace;
    int saves = 0;
};

class ControlledProcess final : public services::IManagedProcess {
public:
    using services::IManagedProcess::IManagedProcess;
    int starts = 0, stops = 0; qint64 pid = 0;
    void start() override { ++starts; }
    void stop(int) override { ++stops; }
    qint64 processId() const override { return pid; }
    void running(qint64 value = 123) { pid = value; emit started(); }
    void exited(int code = 0) { pid = 0; emit finished(code, 0); }
    void failed() { emit processError(QStringLiteral("PRIVATE_PROCESS_ERROR")); }
};
class ProcessLauncher final : public services::IProcessLauncher
{
public:
    services::IManagedProcess* launch(const domain::services::ServiceDefinition&, QObject* owner) override {
        auto* process = new ControlledProcess(owner); processes.append(process); ++launches; return process;
    }
    QList<QPointer<ControlledProcess>> processes; int launches = 0;
};

class NotesRepo final : public notes::INotesRepository
{
public:
    shared::common::Result<QVector<notes::NoteNode>> list(const QString&, bool) override
    { ++listCalls; return shared::common::Result<QVector<notes::NoteNode>>::success(nodes); }
    shared::common::Result<notes::NoteNode> create(const QString&, QString, QString, notes::NoteNodeKind) override { return shared::common::Result<notes::NoteNode>::failure(failure()); }
    shared::common::Result<void> rename(const QString&, const QString&, QString) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> move(const QString&, const QString&, QString) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> setPinned(const QString&, const QString&, bool) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> setTags(const QString&, const QString&, QStringList) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<QString> readContent(const QString&, const QString&) override { return shared::common::Result<QString>::success(content); }
    shared::common::Result<QString> readContentBounded(const QString& workspace, const QString& id, qint64 cap) override
    { ++boundedReads; lastWorkspace = workspace; lastId = id; lastCap = cap; return shared::common::Result<QString>::success(content.left(static_cast<qsizetype>(cap))); }
    shared::common::Result<QString> saveContent(const QString&, const QString& id, const QString& text, const QString& hash) override {
        ++saves;
        if (hash != QString::fromLatin1(QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex())) return shared::common::Result<QString>::failure(failure());
        content = text; const auto next = QString::fromLatin1(QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex());
        for (auto& node : nodes) if (node.id == id) node.contentHash = next;
        return shared::common::Result<QString>::success(next);
    }
    QString contentPath(const QString&, const QString&) const override { return notePath; }
    shared::common::Result<void> setTrashed(const QString&, const QString&, bool) override { return shared::common::Result<void>::failure(failure()); }
    shared::common::Result<void> permanentlyRemove(const QString&, const QString&) override { return shared::common::Result<void>::failure(failure()); }
    QVector<notes::NoteNode> nodes;
    QString content = QStringLiteral("private note body");
    QString notePath;
    QString lastWorkspace, lastId;
    qint64 lastCap = 0;
    int listCalls = 0, boundedReads = 0;
    int saves = 0;
};

struct MutationFixture {
    devhub::tests::assistant::Harness h;
    domain::account::AccountId account = domain::account::AccountId::fromString("92f343d4-5b1e-41ac-9c83-99f6410e1b3f").value();
    domain::workspace::WorkspaceId workspaceId = domain::workspace::WorkspaceId::fromString("8a0d03aa-6cd0-46b6-941b-2b362053d97f").value();
    domain::workspace::WorkspaceId targetId = domain::workspace::WorkspaceId::fromString("f93e9980-e56d-4e64-b855-5fe97977d0e1").value();
    WorkspaceRepo workspaceRepo; workspaces::WorkspaceValidator validator;
    workspaces::WorkspaceManager manager;
    workspaces::WorkspaceContext context;
    ServiceRepo serviceRepo; ProcessLauncher launcher; services::ServiceCoordinator services;
    NotesRepo notes; tasks::TaskScheduler scheduler;
    AssistantToolRuntime runtime;
    bool dirty = false, transitionAllowed = true; int editRequests = 0, transitionRequests = 0;
    explicit MutationFixture(QString version) : h({}, {}, nullptr, version), manager(workspaceRepo, validator, h.ids, h.clock, account),
        services(serviceRepo, launcher), scheduler(h.ids), runtime(h.client, h.controller, scheduler, manager, context, serviceRepo, services, notes) {
        const auto root = QFileInfo(h.scratch.path()).canonicalFilePath();
        workspaceRepo.rows.append(domain::workspace::Workspace(workspaceId, account, "Current", {root, root}, domain::workspace::WorkspaceStatus::Ready, false, 0, h.clock.utcNow()));
        const auto targetPath = QDir(root).filePath("target"); QDir().mkpath(targetPath);
        workspaceRepo.rows.append(domain::workspace::Workspace(targetId, account, "Target", {targetPath, QFileInfo(targetPath).canonicalFilePath()}, domain::workspace::WorkspaceStatus::Ready, false, 0, h.clock.utcNow()));
        context.activate(workspaceRepo.rows.first());
        h.controller.setPersistenceContext(account.toString(), false);
        h.controller.setContext({workspaceId.toString(), "Current", root});
        QObject::connect(&context, &workspaces::WorkspaceContext::workspaceChanged, &h.controller, [this] {
            const auto& active = context.activeWorkspace();
            h.controller.setContext(active ? AssistantContext{active->id().toString(), active->displayName(), active->path().canonicalPath()} : AssistantContext{});
        });
        notes::NoteNode node; node.id = "note-id"; node.workspaceId = workspaceId.toString(); node.title = "Note";
        node.contentHash = QString::fromLatin1(QCryptographicHash::hash(notes.content.toUtf8(), QCryptographicHash::Sha256).toHex()); notes.nodes.append(node);
        domain::services::ServiceDefinition definition; definition.id = "service-id"; definition.workspaceId = workspaceId.toString();
        definition.name = "Test service"; definition.program = QCoreApplication::applicationFilePath(); definition.workingDirectory = root; serviceRepo.rows.append(definition);
        runtime.setMutationIntegration({[this](const QString& ws, const QString& id) { return !dirty && ws == workspaceId.toString() && id == "note-id"; },
            [this](const QString& ws, const QString& id, const QString& text, const QString& hash) { ++editRequests; return notes.saveContent(ws, id, text, hash); },
            [this](const QString& id, auto revalidate, auto committed) {
                ++transitionRequests;
                if (!transitionAllowed || !revalidate()) return false;
                const auto activated = manager.activate(domain::workspace::WorkspaceId::fromString(id).value());
                if (!activated) return false;
                committed(); context.activate(activated.value()); return true;
            }});
    }
    bool ready() { return h.readyController() && h.sendAndAccept(); }
    QJsonObject arguments(const QString& tool) const {
        if (tool == "devhub_notes_edit") return {{"noteId", "note-id"}, {"expectedHash", notes.nodes.first().contentHash}, {"content", "PROPOSED_PRIVATE_CONTENT"}};
        if (tool == "devhub_project_open") return {{"workspaceId", targetId.toString()}};
        return {{"serviceId", "service-id"}};
    }
    void call(const QString& tool, const QJsonObject& args, QString id = "first", QString callId = "action") {
        h.transport.deliver({{"id", id}, {"method", "item/tool/call"}, {"params", QJsonObject{
            {"threadId", "thread-1"}, {"turnId", "turn-1"}, {"callId", callId}, {"tool", tool}, {"arguments", args}}}});
    }
    QJsonObject response(QString id = "first") const {
        for (const auto& message : h.transport.sent) if (message.value("id") == id && message.contains("result")) {
            auto result = message.value("result").toObject();
            return QJsonDocument::fromJson(result.value("contentItems").toArray().first().toObject().value("text").toString().toUtf8()).object();
        }
        return {};
    }
};

QJsonObject responseBody(const QList<QPair<QJsonValue, QJsonObject>>& replies, int index = -1)
{
    if (index < 0) index = static_cast<int>(replies.size()) - 1;
    const auto content = replies.at(index).second.value(QStringLiteral("contentItems")).toArray().first().toObject().value(QStringLiteral("text")).toString();
    return QJsonDocument::fromJson(content.toUtf8()).object();
}

// Observe existing GitService command diagnostics; use the production service
// and runner rather than replacing either implementation with a test double.
class GitInvocationObserver final {
public:
    GitInvocationObserver() { current = this; previous = qInstallMessageHandler(message); }
    ~GitInvocationObserver() { qInstallMessageHandler(previous); current = nullptr; }
    std::atomic_int status{0}, branches{0}, history{0};
private:
    static inline GitInvocationObserver* current = nullptr;
    QtMessageHandler previous = nullptr;
    static void message(QtMsgType type, const QMessageLogContext& context, const QString& text) {
        if (current) {
            if (text.startsWith("Git operation=status ")) ++current->status;
            if (text.startsWith("Git operation=branch-list ")) ++current->branches;
            if (text.startsWith("Git operation=history ")) ++current->history;
            if (current->previous) current->previous(type, context, text);
        }
    }
};
}

class AssistantToolRuntimeTest final : public QObject
{
    Q_OBJECT
private slots:
    void revokedTaskLookupsDoNotPublishLateStatus_data() {
        QTest::addColumn<QString>("tool");
        QTest::addColumn<QString>("transition");
        for (const auto* tool : {"devhub_something_tasks_find", "devhub_something_task_read"})
            for (const auto* transition : {"chat", "workspace", "profile", "grant-roundtrip", "lock", "stop", "finished"})
                QTest::newRow(qPrintable(QString::fromLatin1(tool) + QLatin1Char('-') + QString::fromLatin1(transition)))
                    << QString::fromLatin1(tool) << QString::fromLatin1(transition);
    }

    void revokedTaskLookupsDoNotPublishLateStatus() {
        QFETCH(QString, tool); QFETCH(QString, transition);
        MutationFixture f(QStringLiteral("0.162.0-alpha.17.2")); QVERIFY(f.ready());
        f.scheduler.setGlobalConcurrencyLimit(1);
        f.runtime.setSomethingTasksProfilePath(QDir(f.h.scratch.path()).filePath(QStringLiteral("old-profile.sqlite")));
        f.h.controller.setTaskDataDisclosureGrant(true);
        QSignalSpy lookup(&f.runtime, &AssistantToolRuntime::somethingTasksLookupStatusChanged);
        QSignalSpy connection(&f.runtime, &AssistantToolRuntime::somethingTasksConnectionChanged);

        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        tasks::TaskRequest blocker; blocker.moduleKey = QStringLiteral("something-status-blocker");
        const auto handle = f.scheduler.submit<int>(blocker, [entered, release](const tasks::TaskContext&) {
            entered->release(); release->tryAcquire(1, 5000);
            return shared::common::Result<int>::success(0);
        }, [](auto) {});
        Q_UNUSED(handle);
        // Release even if a test assertion exits early.
        struct ReleaseGuard { std::shared_ptr<QSemaphore> semaphore; ~ReleaseGuard() { semaphore->release(); } } guard{release};
        QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 1000);
        const QJsonObject args = tool == QStringLiteral("devhub_something_tasks_find")
            ? QJsonObject{{QStringLiteral("tags"), QJsonArray{QStringLiteral("OldWorkspaceTag")}}}
            : QJsonObject{{QStringLiteral("taskId"), QStringLiteral("9007199254740993")}};
        f.call(tool, args);
        QCOMPARE(f.runtime.registry().activeCount(), 1);
        if (transition == QStringLiteral("chat")) {
            const auto previous = f.h.controller.conversation().id;
            f.h.controller.newChat(); f.h.finish();
            QVERIFY(previous != f.h.controller.conversation().id);
        } else if (transition == QStringLiteral("workspace")) f.context.activate(f.workspaceRepo.rows.last());
        else if (transition == QStringLiteral("profile"))
            f.runtime.setSomethingTasksProfilePath(QDir(f.h.scratch.path()).filePath(QStringLiteral("new-profile.sqlite")));
        else if (transition == QStringLiteral("grant-roundtrip")) {
            f.h.controller.setTaskDataDisclosureGrant(false);
            f.h.controller.setTaskDataDisclosureGrant(true);
        } else if (transition == QStringLiteral("lock")) f.h.controller.setSessionAllowed(false, true);
        else if (transition == QStringLiteral("stop")) f.h.controller.stopTurn();
        else f.h.finish();
        QCOMPARE(f.runtime.registry().activeCount(), 0);
        const auto resolved = f.response();
        QVERIFY(!resolved.isEmpty()); QVERIFY(!resolved.value(QStringLiteral("success")).toBool());
        const auto lookupCount = lookup.size();
        const auto connectionCount = connection.size();
        const auto status = f.runtime.somethingTasksConnectionStatus();
        release->release();
        QTRY_COMPARE_WITH_TIMEOUT(f.scheduler.activeTaskCount(), 0, 3000);
        QCoreApplication::processEvents();
        QCOMPARE(f.response(), resolved);
        QCOMPARE(lookup.size(), lookupCount);
        QCOMPARE(connection.size(), connectionCount);
        QCOMPARE(f.runtime.somethingTasksConnectionStatus(), status);
    }

    void failedTaskSchedulingNeverReportsRetrieved_data() {
        QTest::addColumn<QString>("tool");
        QTest::newRow("find") << QStringLiteral("devhub_something_tasks_find");
        QTest::newRow("read") << QStringLiteral("devhub_something_task_read");
    }

    void failedTaskSchedulingNeverReportsRetrieved() {
        QFETCH(QString, tool);
        MutationFixture f(QStringLiteral("0.162.0-alpha.17.2")); QVERIFY(f.ready());
        f.runtime.setSomethingTasksProfilePath(QDir(f.h.scratch.path()).filePath(QStringLiteral("synthetic-missing.sqlite")));
        f.h.controller.setTaskDataDisclosureGrant(true);
        f.scheduler.stopAcceptingAndCancel();
        QSignalSpy lookup(&f.runtime, &AssistantToolRuntime::somethingTasksLookupStatusChanged);
        f.call(tool, tool == QStringLiteral("devhub_something_tasks_find")
            ? QJsonObject{{QStringLiteral("tags"), QJsonArray{QStringLiteral("DevHub")}}}
            : QJsonObject{{QStringLiteral("taskId"), QStringLiteral("1")}});
        QTRY_VERIFY(!f.response().isEmpty());
        QVERIFY(!f.response().value(QStringLiteral("success")).toBool());
        QCOMPARE(lookup.size(), 1);
        QVERIFY(lookup.first().first().toString().contains(QStringLiteral("failed"), Qt::CaseInsensitive));
        QVERIFY(!lookup.first().first().toString().contains(QStringLiteral("Retrieved")));
    }

    void taskDataDisclosureRevocationRejectsAndDiscardsResults() {
        QList<QPair<QString, ToolResult>> resolved;
        AssistantToolRegistry registry([&](const QString& route, const ToolResult& result) {
            resolved.append({route, result});
        });
        std::function<void(ToolResult)> finish;
        int entered = 0;
        ToolDefinition definition;
        definition.name = QStringLiteral("devhub_something_tasks_find");
        definition.description = QStringLiteral("Read synthetic task data for a registry contract test.");
        definition.allowedArguments = {QStringLiteral("tags")};
        definition.requiresWorkspace = true;
        definition.requiresTaskDataReadDisclosure = true;
        definition.validateArguments = [](const QJsonObject& input, QJsonObject* normalized) {
            *normalized = input; return true;
        };
        QVERIFY(registry.registerTool(definition, [&](const ToolCall&, const ToolCancellation&,
            std::function<void(ToolResult)> done) { ++entered; finish = std::move(done); }));

        ToolCall denied;
        denied.routeId = QStringLiteral("denied-route"); denied.callId = QStringLiteral("denied-call");
        denied.name = definition.name;
        denied.scope = ToolScope(QStringLiteral("scope-a"), QStringLiteral("account-a"), QStringLiteral("workspace-a"),
            QStringLiteral("C:/workspace"), 1, 1, true, {}, false, false, false);
        QVERIFY(registry.invoke(denied));
        QCOMPARE(entered, 0);
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved.last().second.errorCode, QStringLiteral("scope_denied"));

        ToolCall allowed;
        allowed.routeId = QStringLiteral("allowed-route"); allowed.callId = QStringLiteral("allowed-call");
        allowed.name = definition.name;
        allowed.scope = ToolScope(QStringLiteral("scope-b"), QStringLiteral("account-a"), QStringLiteral("workspace-a"),
            QStringLiteral("C:/workspace"), 2, 1, true, {}, false, false, true);
        QVERIFY(registry.invoke(allowed));
        QCOMPARE(entered, 1);
        QVERIFY(bool(finish));
        registry.cancelAll(QStringLiteral("task_data_disclosure_changed"));
        QCOMPARE(resolved.size(), 2);
        QCOMPARE(resolved.last().first, QStringLiteral("allowed-route"));
        QCOMPARE(resolved.last().second.errorCode, QStringLiteral("task_data_disclosure_changed"));
        ToolResult late; late.success = true; late.data.insert(QStringLiteral("title"), QStringLiteral("stale task title"));
        finish(std::move(late));
        QCOMPARE(resolved.size(), 2);
    }

    void advertisedInventoryMatchesNativeAlphaEvidence() {
        QFile file(QFINDTESTDATA("fixtures/codex/0.162.0-alpha.17.2/phase9-tool-specifications.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonArray advertised = AssistantToolRuntime::experimentalToolSpecifications();
        for (qsizetype i = advertised.size() - 1; i >= 0; --i) {
            const QString name = advertised.at(i).toObject().value(QStringLiteral("name")).toString();
            if (name == QStringLiteral("devhub_something_tasks_find")
                || name == QStringLiteral("devhub_something_task_read")) advertised.removeAt(i);
        }
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).array(), advertised);
        const auto all = AssistantToolRuntime::experimentalToolSpecifications();
        QStringList taskTools;
        for (const auto& item : all) {
            const QString name = item.toObject().value(QStringLiteral("name")).toString();
            if (name.startsWith(QStringLiteral("devhub_something_task"))) taskTools.append(name);
        }
        QCOMPARE(taskTools, QStringList({QStringLiteral("devhub_something_tasks_find"), QStringLiteral("devhub_something_task_read")}));
    }
    void selectedMutations_data() {
        QTest::addColumn<QString>("tool"); QTest::addColumn<QString>("version"); QTest::addColumn<QString>("scenario");
        for (const auto& version : {"0.160.1", "0.162.0-alpha.17.2"})
            for (const auto& tool : {"devhub_notes_edit", "devhub_services_start", "devhub_services_stop", "devhub_services_restart", "devhub_project_open"})
                for (const auto& scenario : {"allow", "deny", "expiry", "lock", "stop", "crash", "context"})
                    QTest::newRow(qPrintable(QString::fromLatin1(version) + '-' + tool + '-' + scenario)) << QString::fromLatin1(tool) << QString::fromLatin1(version) << QString::fromLatin1(scenario);
    }
    void selectedMutations() {
        QFETCH(QString, tool); QFETCH(QString, version); QFETCH(QString, scenario);
        MutationFixture f(version); QVERIFY(f.ready());
        if (tool == "devhub_services_stop" || tool == "devhub_services_restart") {
            QVERIFY(f.services.start(f.serviceRepo.rows.first(), f.h.scratch.path()));
            f.launcher.processes.last()->running();
        }
        const int initialLaunches = f.launcher.launches, initialSaves = f.serviceRepo.saves;
        const auto args = f.arguments(tool); f.call(tool, args); f.call(tool, args, "alias");
        QCOMPARE(f.runtime.approvals().pending().size(), 1); QVERIFY(f.response().isEmpty());
        QCOMPARE(f.notes.saves, 0); QCOMPARE(f.launcher.launches, initialLaunches); QCOMPARE(f.workspaceRepo.opened, 0);
        const auto id = f.runtime.approvals().pending().first().id;
        if (scenario == "expiry") f.runtime.approvals().expireNow(100000);
        else if (scenario == "lock") f.h.controller.setSessionAllowed(false);
        else if (scenario == "stop") f.h.controller.stopTurn();
        else if (scenario == "crash") emit f.h.transport.stopped(1, true, f.h.transport.generation);
        else if (scenario == "context") f.context.clear();
        else QVERIFY(f.runtime.approvals().decide(id, scenario == "allow"));
        QCOMPARE(f.runtime.approvals().pending().size(), 0);
        QVERIFY(!f.runtime.approvals().decide(id, true));
        if (scenario == "allow" && tool.startsWith("devhub_services_")) {
            QVERIFY(f.response().isEmpty()); // Admission must not look like completion.
            if (tool == "devhub_services_start") { QCOMPARE(f.launcher.launches, initialLaunches + 1); f.launcher.processes.last()->running(); }
            else if (tool == "devhub_services_stop") { QCOMPARE(f.launcher.processes.first()->stops, 1); f.launcher.processes.first()->exited(); }
            else {
                auto old = f.launcher.processes.first(); QCOMPARE(old->stops, 1); old->exited();
                QCOMPARE(f.launcher.launches, initialLaunches + 1); QVERIFY(f.response().isEmpty());
                f.launcher.processes.last()->running(456);
            }
        }
        if (scenario != "crash") {
            QTRY_VERIFY(!f.response().isEmpty()); QCOMPARE(f.response("alias"), f.response());
            QCOMPARE(f.response().value("success").toBool(), scenario == "allow");
            if ((scenario == "allow" && tool != "devhub_project_open") || scenario == "deny" || scenario == "expiry") {
                f.call(tool, args, "after"); QCOMPARE(f.response("after"), f.response());
            }
        }
        QCOMPARE(f.notes.saves, scenario == "allow" && tool == "devhub_notes_edit" ? 1 : 0);
        QCOMPARE(f.editRequests, f.notes.saves);
        QCOMPARE(f.workspaceRepo.opened, scenario == "allow" && tool == "devhub_project_open" ? 1 : 0);
        QCOMPARE(f.transitionRequests, f.workspaceRepo.opened);
        QCOMPARE(f.serviceRepo.saves - initialSaves, scenario == "allow" && (tool == "devhub_services_start" || tool == "devhub_services_restart") ? 1 : 0);
        QCOMPARE(f.launcher.launches - initialLaunches, scenario == "allow" && (tool == "devhub_services_start" || tool == "devhub_services_restart") ? 1 : 0);
        // Count required admission/revalidation reads as well as effects. Aliases
        // cause no additional reads or writes, including after completion.
        QCOMPARE(f.notes.listCalls, tool == "devhub_notes_edit" ? (scenario == "allow" ? 2 : 1) : 0);
        QCOMPARE(f.serviceRepo.listCalls, tool.startsWith("devhub_services_")
            ? (scenario == "allow" ? (tool == "devhub_services_restart" ? 3 : 2) : 1) : 0);
        QCOMPARE(f.workspaceRepo.listCalls, tool == "devhub_project_open" ? (scenario == "allow" ? 4 : 1) : 0);
        for (const auto& message : f.h.controller.messages()) QVERIFY(!message.source.contains("PROPOSED_PRIVATE_CONTENT"));
    }

    void revalidateMutations_data() {
        QTest::addColumn<QString>("scenario");
        for (const auto& s : {"dirty", "note-hash", "external-note", "service-definition", "service-pid", "transition-denied", "workspace-path", "delayed-start-failure", "restart-crash", "restart-definition", "restart-other-action", "start-stop", "cancel-auto-restart", "unknown-effect", "invalid-content"}) QTest::newRow(s) << QString::fromLatin1(s);
    }
    void revalidateMutations() {
        QFETCH(QString, scenario); MutationFixture f("0.162.0-alpha.17.2"); QVERIFY(f.ready());
        QString tool = "devhub_notes_edit";
        if (scenario.startsWith("service-") || scenario == "delayed-start-failure" || scenario == "start-stop" || scenario == "cancel-auto-restart") tool = "devhub_services_start";
        if (scenario == "service-pid" || scenario.startsWith("restart-")) {
            tool = "devhub_services_restart"; QVERIFY(f.services.start(f.serviceRepo.rows.first(), f.h.scratch.path())); f.launcher.processes.last()->running();
        }
        if (scenario.startsWith("workspace-") || scenario == "transition-denied") tool = "devhub_project_open";
        if (scenario == "unknown-effect") tool = "devhub_files_delete";
        if (scenario == "cancel-auto-restart") f.serviceRepo.rows.first().restartPolicy = domain::services::RestartPolicy::Always;
        auto args = f.arguments(tool); if (scenario == "invalid-content") args.insert("command", "FORBIDDEN");
        const int initialLaunches = f.launcher.launches;
        f.call(tool, args);
        if (scenario == "unknown-effect" || scenario == "invalid-content") { QCOMPARE(f.runtime.approvals().pending().size(), 0); QVERIFY(!f.response().value("success").toBool()); return; }
        QCOMPARE(f.runtime.approvals().pending().size(), 1);
        const auto id = f.runtime.approvals().pending().first().id;
        if (scenario == "dirty") f.dirty = true;
        if (scenario == "note-hash") f.notes.nodes.first().contentHash = QString(64, 'b');
        if (scenario == "external-note") f.notes.content = "Changed externally";
        if (scenario == "service-definition") f.serviceRepo.rows.first().arguments.append("changed");
        if (scenario == "service-pid") { auto p = f.launcher.processes.last(); p->exited(); QVERIFY(f.services.start(f.serviceRepo.rows.first(), f.h.scratch.path())); f.launcher.processes.last()->running(999); }
        if (scenario == "transition-denied") f.transitionAllowed = false;
        if (scenario == "workspace-path") f.workspaceRepo.rows.removeLast();
        QVERIFY(f.runtime.approvals().decide(id, true));
        if (scenario == "delayed-start-failure") { QVERIFY(f.response().isEmpty()); f.launcher.processes.last()->failed(); }
        if (scenario == "restart-crash") {
            QVERIFY(f.response().isEmpty()); emit f.h.transport.stopped(1, true, f.h.transport.generation); auto old = f.launcher.processes.first(); old->exited();
            QCOMPARE(f.launcher.launches, initialLaunches); QVERIFY(f.response().isEmpty()); return;
        }
        if (scenario == "start-stop") { QVERIFY(f.response().isEmpty()); f.h.controller.stopTurn(); f.launcher.processes.last()->running(); }
        if (scenario == "restart-definition") {
            QVERIFY(f.response().isEmpty()); f.serviceRepo.rows.first().arguments.append("changed-after-stop");
            f.launcher.processes.first()->exited(); QCOMPARE(f.launcher.launches, initialLaunches);
            QCOMPARE(f.response().value("data").toObject().value("outcome").toString(), QString("stopped_without_restart"));
        }
        if (scenario == "restart-other-action") {
            // An external state observer starts a different action after the old
            // process stops. It must not complete this Assistant's restart ticket.
            const auto connection = connect(&f.services, &services::ServiceCoordinator::stateChanged, &f.services,
                [&](const QString&, const services::ServiceRuntimeState& state) {
                    if (state.lifecycle == services::ServiceLifecycleState::Stopped) {
                        const auto started = f.services.start(f.serviceRepo.rows.first(), f.h.scratch.path()); QVERIFY(started);
                        f.launcher.processes.last()->running(999);
                    }
                });
            f.launcher.processes.first()->exited(); disconnect(connection);
        }
        if (scenario == "cancel-auto-restart") {
            QVERIFY(f.response().isEmpty()); f.h.controller.stopTurn(); f.launcher.processes.last()->failed();
            bool restartWindowElapsed = false; QTimer::singleShot(1200, &f.h.controller, [&] { restartWindowElapsed = true; });
            QTRY_VERIFY_WITH_TIMEOUT(restartWindowElapsed, 2000); QCOMPARE(f.launcher.launches, 1);
        }
        QTRY_VERIFY(!f.response().isEmpty()); QVERIFY(!f.response().value("success").toBool());
        if (scenario == "start-stop") QCOMPARE(f.response().value("data").toObject().value("outcome").toString(), QString("uncertain"));
        if (scenario == "external-note") QCOMPARE(f.notes.content, QString("Changed externally"));
        else QCOMPARE(f.notes.saves, 0);
        QCOMPARE(f.workspaceRepo.opened, 0);
    }
    void allReadHandlersExecuteExistingCapabilitiesOnce_data()
    {
        QTest::addColumn<QString>("tool"); QTest::addColumn<QString>("version");
        for (const auto& version : {"0.160.1", "0.162.0-alpha.17.2"})
            for (const auto& spec : AssistantToolRuntime::experimentalToolSpecifications()) {
                const QString tool = spec.toObject().value("name").toString();
                if (tool == "devhub_notes_edit" || tool == "devhub_project_open"
                    || (tool.startsWith("devhub_services_") && tool != "devhub_services_list")) continue;
                QTest::newRow(qPrintable(QString::fromLatin1(version) + '-' + tool)) << tool << QString::fromLatin1(version);
            }
    }

    void allReadHandlersExecuteExistingCapabilitiesOnce()
    {
        QFETCH(QString, tool); QFETCH(QString, version);
        devhub::tests::assistant::Harness h({}, {}, nullptr, version);
        const QString root = QFileInfo(h.scratch.path()).canonicalFilePath();
        const auto accountId = domain::account::AccountId::fromString(QStringLiteral("92f343d4-5b1e-41ac-9c83-99f6410e1b3f")).value();
        const auto workspaceId = domain::workspace::WorkspaceId::fromString(QStringLiteral("8a0d03aa-6cd0-46b6-941b-2b362053d97f")).value();
        const domain::workspace::Workspace workspace(workspaceId, accountId, QStringLiteral("Counts"),
            {root, root}, domain::workspace::WorkspaceStatus::Ready, false, 0, h.clock.utcNow());
        WorkspaceRepo workspaceRepo; workspaceRepo.rows.append(workspace); workspaces::WorkspaceValidator validator;
        workspaces::WorkspaceManager manager(workspaceRepo, validator, h.ids, h.clock, accountId);
        workspaces::WorkspaceContext workspaceContext; workspaceContext.activate(workspace);
        ServiceRepo serviceRepo; ProcessLauncher launcher; services::ServiceCoordinator coordinator(serviceRepo, launcher);
        NotesRepo noteRepo; notes::NoteNode note; note.id = QStringLiteral("note-id");
        note.kind = notes::NoteNodeKind::Note; note.workspaceId = workspaceId.toString(); noteRepo.nodes.append(note);
        noteRepo.notePath = QDir(root).filePath(QStringLiteral("note.md"));
        QFile file(noteRepo.notePath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("private note body"); file.close();
        if (tool.startsWith("devhub_git_")) {
            const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
            if (git.isEmpty()) QSKIP("Git is required for the existing GitService invocation accounting.");
            const auto run = [&](QStringList args) {
                QProcess process; process.setProgram(git); process.setArguments(args); process.setWorkingDirectory(root); process.start();
                return process.waitForStarted(3000) && process.waitForFinished(10000) && process.exitCode() == 0;
            };
            QVERIFY(run({"init", "--quiet"}));
            QVERIFY(run({"-c", "user.name=DevHub test", "-c", "user.email=test@example.invalid", "commit", "--allow-empty", "-m", "fixture", "--quiet"}));
        }
        devhub::tests::assistant::SyntheticProfile profile;
        const bool taskTool = tool.startsWith(QStringLiteral("devhub_something_task"));
        if (taskTool) {
            QVERIFY(profile.created);
            QVERIFY(profile.addTask(9007199254740993LL, QStringLiteral("Synthetic DevHub task")));
            QVERIFY(profile.addTag(9007199254740993LL, QStringLiteral("DevHub")));
        }
        tasks::TaskScheduler scheduler(h.ids);
        AssistantToolRuntime runtime(h.client, h.controller, scheduler, manager, workspaceContext, serviceRepo, coordinator, noteRepo);
        h.controller.setPersistenceContext(accountId.toString(), false);
        h.controller.setContext({workspaceId.toString(), workspace.displayName(), root});
        QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept()); h.controller.setToolDisclosureGrants(true, true);
        if (taskTool) {
            runtime.setSomethingTasksProfilePath(profile.path);
            h.controller.setTaskDataDisclosureGrant(true);
        }
        QSignalSpy activity(&h.controller, &AssistantController::toolActivityChanged);
        GitInvocationObserver gitCalls;
        QJsonObject args;
        if (tool == "devhub_git_branches") args = {{"remote", false}};
        if (tool == "devhub_notes_read") args = {{"noteId", "note-id"}};
        if (tool == "devhub_notes_search") args = {{"query", "private"}};
        if (tool == "devhub_files_read") args = {{"path", "note.md"}};
        if (tool == "devhub_files_search") args = {{"query", "note"}};
        if (tool == "devhub_something_tasks_find") args = {{"tags", QJsonArray{QStringLiteral("DevHub")}}};
        if (tool == "devhub_something_task_read") args = {{"taskId", QStringLiteral("9007199254740993")}};
        const auto deliver = [&](QString id, QString callId, const QString& name, QJsonObject arguments) {
            h.transport.deliver({{"id", id}, {"method", "item/tool/call"}, {"params", QJsonObject{
                {"threadId", "thread-1"}, {"turnId", "turn-1"}, {"callId", callId}, {"tool", name}, {"arguments", arguments}}}});
        };
        const auto response = [&](const QString& id) {
            for (const auto& message : h.transport.sent)
                if (message.value("id") == QJsonValue(id) && message.contains("result")) return message.value("result").toObject();
            return QJsonObject{};
        };
        QJsonObject nativeItem{{"type", "dynamicToolCall"}, {"id", "native-tool-item"}, {"tool", tool},
            {"arguments", args}, {"status", "inProgress"}};
        h.item("item/started", nativeItem);
        QVERIFY(h.client.isInitialized());
        deliver("first", "same-call", tool, args);
        deliver("in-flight-alias", "same-call", tool, args);
        QTRY_VERIFY_WITH_TIMEOUT(!response("first").isEmpty(), 5000);
        QVERIFY(response("first").value("success").toBool());
        if (taskTool) {
            const auto wireResponse = response("first");
            const auto text = wireResponse.value(QStringLiteral("contentItems")).toArray().first().toObject()
                .value(QStringLiteral("text")).toString();
            const auto body = QJsonDocument::fromJson(text.toUtf8()).object();
            QVERIFY(body.value(QStringLiteral("success")).toBool());
            const auto data = body.value(QStringLiteral("data")).toObject();
            QCOMPARE(data.value(QStringLiteral("source")).toString(), QStringLiteral("SomethingQt"));
            const auto task = tool == QStringLiteral("devhub_something_tasks_find")
                ? data.value(QStringLiteral("tasks")).toArray().first().toObject()
                : data.value(QStringLiteral("task")).toObject();
            QCOMPARE(task.value(QStringLiteral("taskId")).toString(), QStringLiteral("9007199254740993"));
            QCOMPARE(task.value(QStringLiteral("title")).toString(), QStringLiteral("Synthetic DevHub task"));
        }
        QCOMPARE(response("in-flight-alias"), response("first"));
        deliver("completed-alias", "same-call", tool, args);
        QCOMPARE(response("completed-alias"), response("first"));
        nativeItem.insert("status", "completed"); nativeItem.insert("success", true);
        nativeItem.insert("contentItems", QJsonArray{QJsonObject{{"type", "inputText"}, {"text", "PRIVATE_TOOL_BODY"}}});
        h.item("item/completed", nativeItem);
        auto conflicting = args; conflicting.insert("unexpected", true);
        deliver("invalid", "invalid-call", tool, conflicting);
        QVERIFY(!response("invalid").value("success").toBool());
        deliver("unknown", "unknown-call", QStringLiteral("devhub_unknown"), {});
        QVERIFY(!response("unknown").value("success").toBool());
        h.delta("Done");
        h.finish("completed", QJsonArray{nativeItem, devhub::tests::codex::agent("agent-1", "Done")});
        QVERIFY(h.client.isInitialized()); QCOMPARE(h.controller.messages().size(), 2);
        for (const auto& message : h.controller.messages()) {
            QVERIFY(!message.source.contains("PRIVATE_TOOL_BODY"));
            QVERIFY(!message.source.contains("private note body"));
        }
        QTRY_COMPARE_WITH_TIMEOUT(scheduler.activeTaskCount(), 0, 3000);
        QCOMPARE(activity.size(), 2);
        QCOMPARE(activity.first().at(0).toString(), AssistantToolRuntime::internalName(tool));
        QVERIFY(activity.first().at(1).toBool()); QVERIFY(!activity.last().at(1).toBool()); QVERIFY(activity.last().at(2).toBool());
        QCOMPARE(workspaceRepo.listCalls, tool == "devhub_workspace_metadata" ? 1 : 0);
        QCOMPARE(serviceRepo.listCalls, tool == "devhub_services_list" ? 1 : 0);
        QCOMPARE(noteRepo.boundedReads, tool == "devhub_notes_read" ? 1 : 0);
        QCOMPARE(noteRepo.listCalls, tool == "devhub_notes_search" ? 1 : 0);
        QCOMPARE(gitCalls.status.load(), tool == "devhub_git_status" ? 1 : 0);
        QCOMPARE(gitCalls.branches.load(), tool == "devhub_git_branches" ? 1 : 0);
        QCOMPARE(gitCalls.history.load(), tool == "devhub_git_log" ? 1 : 0);
    }

    void integratedReadLifecycle_data()
    {
        QTest::addColumn<QString>("tool");
        QTest::addColumn<QString>("transition");
        QTest::addColumn<QString>("version");
        for (const auto& version : {"0.160.1", "0.162.0-alpha.17.2"})
        for (const auto& tool : {"devhub_files_read", "devhub_files_search", "devhub_notes_search"})
            for (const auto& transition : {"denial", "lock", "crash", "stop", "disclosure", "completed", "workspace"})
                QTest::newRow(qPrintable(QString::fromLatin1(version) + '-' + tool + '-' + transition))
                    << QString::fromLatin1(tool) << QString::fromLatin1(transition) << QString::fromLatin1(version);
    }

    void integratedReadLifecycle()
    {
        QFETCH(QString, tool); QFETCH(QString, transition);
        QFETCH(QString, version);
        devhub::tests::assistant::Harness h({}, {}, nullptr, version);
        QVERIFY(h.scratch.isValid());
        const auto accountId = domain::account::AccountId::fromString(QStringLiteral("92f343d4-5b1e-41ac-9c83-99f6410e1b3f")).value();
        const auto workspaceId = domain::workspace::WorkspaceId::fromString(QStringLiteral("8a0d03aa-6cd0-46b6-941b-2b362053d97f")).value();
        const QString root = QFileInfo(h.scratch.path()).canonicalFilePath();
        const domain::workspace::Workspace workspace(workspaceId, accountId, QStringLiteral("Lifecycle"),
            {root, root}, domain::workspace::WorkspaceStatus::Ready, false, 0, h.clock.utcNow());
        WorkspaceRepo workspaceRepo; workspaceRepo.rows.append(workspace);
        workspaces::WorkspaceValidator validator;
        workspaces::WorkspaceManager manager(workspaceRepo, validator, h.ids, h.clock, accountId);
        workspaces::WorkspaceContext workspaceContext; workspaceContext.activate(workspace);
        ServiceRepo serviceRepo; ProcessLauncher launcher; services::ServiceCoordinator coordinator(serviceRepo, launcher);
        NotesRepo noteRepo;
        notes::NoteNode node; node.id = QStringLiteral("note-id"); node.kind = notes::NoteNodeKind::Note;
        node.workspaceId = workspaceId.toString(); noteRepo.nodes.append(node);
        noteRepo.notePath = QDir(root).filePath(QStringLiteral("private.txt"));
        QFile file(noteRepo.notePath); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("PRIVATE_LIFECYCLE_BODY"); file.close();
        tasks::TaskScheduler scheduler(h.ids, 1);
        AssistantToolRuntime runtime(h.client, h.controller, scheduler, manager, workspaceContext,
            serviceRepo, coordinator, noteRepo);
        h.controller.setPersistenceContext(accountId.toString(), false);
        h.controller.setContext({workspaceId.toString(), workspace.displayName(), root});
        QVERIFY(h.readyController()); QVERIFY(h.sendAndAccept());
        h.controller.setToolDisclosureGrants(transition != "denial", transition != "denial");
        QSignalSpy activity(&h.controller, &AssistantController::toolActivityChanged);

        // Hold the single worker before dispatch; release it after revocation to
        // exercise a real queued read and its late completion deterministically.
        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        tasks::TaskRequest blocker; blocker.moduleKey = QStringLiteral("lifecycle-blocker");
        bool blockerFinished = false;
        const auto handle = scheduler.submit<int>(blocker, [entered, release](const tasks::TaskContext&) {
            entered->release(); release->tryAcquire(1, 5000);
            return shared::common::Result<int>::success(0);
        }, [&blockerFinished](auto) { blockerFinished = true; });
        Q_UNUSED(handle);
        QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 1000);
        const QJsonObject arguments = tool == "devhub_files_read"
            ? QJsonObject{{"path", "private.txt"}} : QJsonObject{{"query", "PRIVATE_LIFECYCLE_BODY"}};
        const auto deliver = [&](QString id, QString callId) {
            h.transport.deliver({{"id", id}, {"method", "item/tool/call"}, {"params", QJsonObject{
                {"threadId", "thread-1"}, {"turnId", "turn-1"}, {"callId", callId},
                {"tool", tool}, {"arguments", arguments}}}});
        };
        const auto responses = [&] {
            QList<QJsonObject> result;
            for (const auto& message : h.transport.sent)
                if (message.value("id") == QJsonValue("lifecycle-request") && message.contains("result")) result.append(message);
            return result;
        };
        deliver("lifecycle-request", "lifecycle-call");
        // File-name search does not require body disclosure.
        const bool denied = transition == "denial" && tool != "devhub_files_search";
        QCOMPARE(runtime.registry().activeCount(), denied ? 0 : 1);
        if (!denied) {
            if (transition == "lock") h.controller.setSessionAllowed(false, true);
            else if (transition == "crash") emit h.transport.stopped(1, true, h.transport.generation);
            else if (transition == "stop") h.controller.stopTurn();
            else if (transition == "disclosure") h.controller.setToolDisclosureGrants(false, false);
            else if (transition == "completed") h.finish();
            else if (transition == "workspace") workspaceContext.clear();
        }
        const bool cancelled = !denied && transition != "denial";
        if (cancelled) QCOMPARE(runtime.registry().activeCount(), 0);
        const auto responseCount = responses().size();
        release->release();
        QTRY_VERIFY_WITH_TIMEOUT(blockerFinished, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(scheduler.activeTaskCount(), 0, 3000);
        QCoreApplication::processEvents();
        if (transition == "crash") QCOMPARE(responses().size(), 0);
        else {
            QCOMPARE(responses().size(), 1);
            QCOMPARE(responses().first().value("result").toObject().value("success").toBool(), !denied && !cancelled);
        }
        if (cancelled) QCOMPARE(responses().size(), responseCount);
        for (const auto& message : responses())
            QVERIFY(!QJsonDocument(message).toJson().contains("PRIVATE_LIFECYCLE_BODY"));
        for (const auto& event : activity) {
            QVERIFY(!event.at(0).toString().contains("PRIVATE_LIFECYCLE_BODY"));
            if (!event.at(1).toBool() && cancelled) QVERIFY(!event.at(2).toBool());
        }
        QCOMPARE(activity.size(), denied ? 0 : 2);
        if (transition == "lock") {
            QVERIFY(!h.controller.notesDisclosureGranted()); QVERIFY(!h.controller.filesDisclosureGranted());
        }
        if (transition == "stop") {
            // A fresh call during cancellation must also be denied.
            deliver("after-stop", "after-stop-call");
            QCOMPARE(runtime.registry().activeCount(), 0);
            QVERIFY(!h.transport.sent.back().value("result").toObject().value("success").toBool());
        }
    }

    void ownerHandlersReuseRepositoriesAndStripSecrets()
    {
        QTemporaryDir workspaceRoot; QVERIFY(workspaceRoot.isValid());
        const auto accountId = domain::account::AccountId::fromString(QStringLiteral("92f343d4-5b1e-41ac-9c83-99f6410e1b3f")).value();
        const auto workspaceId = domain::workspace::WorkspaceId::fromString(QStringLiteral("8a0d03aa-6cd0-46b6-941b-2b362053d97f")).value();
        const QString canonicalRoot = QFileInfo(workspaceRoot.path()).canonicalFilePath();
        const domain::workspace::Workspace workspace(workspaceId, accountId, QStringLiteral("Sample workspace"),
            {workspaceRoot.path(), canonicalRoot}, domain::workspace::WorkspaceStatus::Ready, true, 0, QDateTime::currentDateTimeUtc());
        WorkspaceRepo workspaceRepo; workspaceRepo.rows.append(workspace);
        workspaces::WorkspaceValidator validator;
        shared::identifiers::UuidIdGenerator ids;
        shared::time::SystemApplicationClock clock;
        workspaces::WorkspaceManager manager(workspaceRepo, validator, ids, clock, accountId);
        workspaces::WorkspaceContext workspaceContext;
        workspaceContext.activate(workspace);

        ServiceRepo serviceRepo;
        domain::services::ServiceDefinition service;
        service.id = QStringLiteral("service-id"); service.workspaceId = workspaceId.toString();
        service.name = QStringLiteral("Local API"); service.program = QStringLiteral("PLANTED_COMMAND_SECRET");
        service.environmentNames = {QStringLiteral("PLANTED_ENV_SECRET")};
        serviceRepo.rows.append(service);
        ProcessLauncher launcher;
        services::ServiceCoordinator coordinator(serviceRepo, launcher);
        NotesRepo noteRepo;
        notes::NoteNode note; note.id = QStringLiteral("note-id"); note.workspaceId = workspaceId.toString();
        note.title = QStringLiteral("Note"); note.kind = notes::NoteNodeKind::Note;
        noteRepo.nodes.append(note);
        noteRepo.notePath = QDir(canonicalRoot).filePath(QStringLiteral("note.md"));
        QFile noteFile(noteRepo.notePath); QVERIFY(noteFile.open(QIODevice::WriteOnly)); noteFile.write("private note body"); noteFile.close();

        tasks::TaskScheduler scheduler(ids);
        FakeToolClient client;
        AssistantController controller(client, clock, ids, workspaceRoot.path(), {}, QProcessEnvironment{});
        controller.applyConfiguration({QStringLiteral("codex.exe"), {}});
        QSignalSpy probe(&controller, &AssistantController::executableProbeRequested);
        controller.open(); QCOMPARE(probe.size(), 1);
        controller.setPersistenceContext(accountId.toString(), false);
        controller.setContext({workspaceId.toString(), workspace.displayName(), canonicalRoot});
        controller.executableFound({QStringLiteral("C:/fixture/codex.exe"), QStringLiteral("0.160.1")},
            probe.front().at(1).toULongLong());
        QCOMPARE(client.generation, quint64(1));

        AssistantToolRuntime runtime(client, controller, scheduler, manager, workspaceContext,
            serviceRepo, coordinator, noteRepo);
        QVERIFY(runtime.enabledForTestedWireProfile());
        QCOMPARE(client.specifications, AssistantToolRuntime::experimentalToolSpecifications());
        client.call(11, QStringLiteral("devhub_workspace_metadata"));
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 1, 1000);
        QCOMPARE(workspaceRepo.listCalls, 1);
        auto body = responseBody(client.replies);
        QVERIFY(body.value(QStringLiteral("success")).toBool());
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("name")).toString(), workspace.displayName());
        QVERIFY(!body.value(QStringLiteral("data")).toObject().contains(QStringLiteral("path")));

        client.call(12, QStringLiteral("devhub_services_list"));
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 2, 1000);
        QCOMPARE(serviceRepo.listCalls, 1); QCOMPARE(serviceRepo.lastWorkspace, workspaceId.toString());
        body = responseBody(client.replies);
        QVERIFY(body.value(QStringLiteral("success")).toBool());
        const auto publicService = body.value(QStringLiteral("data")).toObject().value(QStringLiteral("services")).toArray().first().toObject();
        QCOMPARE(publicService.value(QStringLiteral("name")).toString(), QStringLiteral("Local API"));
        QCOMPARE(publicService.value(QStringLiteral("state")).toString(), QStringLiteral("stopped"));
        QVERIFY(!QJsonDocument(publicService).toJson().contains("PLANTED_COMMAND_SECRET"));
        QVERIFY(!QJsonDocument(publicService).toJson().contains("PLANTED_ENV_SECRET"));

        client.call(13, QStringLiteral("devhub_notes_read"), {{QStringLiteral("noteId"), QStringLiteral("note-id")}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 3, 1000);
        QCOMPARE(noteRepo.boundedReads, 0);
        body = responseBody(client.replies);
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("error")).toString(), QStringLiteral("scope_denied"));
        controller.setToolDisclosureGrants(true, false);
        client.call(14, QStringLiteral("devhub_notes_read"), {{QStringLiteral("noteId"), QStringLiteral("note-id")}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 4, 1000);
        QCOMPARE(noteRepo.boundedReads, 1); QCOMPARE(noteRepo.lastCap, qint64(16 * 1024));
        QCOMPARE(noteRepo.lastWorkspace, workspaceId.toString()); QCOMPARE(noteRepo.lastId, QStringLiteral("note-id"));
        QVERIFY(responseBody(client.replies).value(QStringLiteral("success")).toBool());
        client.call(15, QStringLiteral("devhub_notes_search"), {{QStringLiteral("query"), QStringLiteral("private note")}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 5, 3000);
        QCOMPARE(noteRepo.listCalls, 1);
        const auto noteIds = responseBody(client.replies, 4).value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("noteIds")).toArray();
        QCOMPARE(noteIds, QJsonArray{QStringLiteral("note-id")});
        controller.setPersistenceContext(QStringLiteral("different-account"), true);
        client.call(16, QStringLiteral("devhub_services_list"));
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 6, 1000);
        QCOMPARE(responseBody(client.replies, 5).value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("error")).toString(), QStringLiteral("scope_denied"));
        QCOMPARE(serviceRepo.listCalls, 1);
    }

    void explicitFileDisclosureUsesSharedBoundedService()
    {
        QTemporaryDir workspaceRoot; QVERIFY(workspaceRoot.isValid());
        const auto accountId = domain::account::AccountId::fromString(QStringLiteral("92f343d4-5b1e-41ac-9c83-99f6410e1b3f")).value();
        const auto workspaceId = domain::workspace::WorkspaceId::fromString(QStringLiteral("8a0d03aa-6cd0-46b6-941b-2b362053d97f")).value();
        const QString root = QFileInfo(workspaceRoot.path()).canonicalFilePath();
        const auto workspace = domain::workspace::Workspace(workspaceId, accountId, QStringLiteral("Files"),
            {workspaceRoot.path(), root}, domain::workspace::WorkspaceStatus::Ready, false, 0, QDateTime::currentDateTimeUtc());
        WorkspaceRepo workspaceRepo; workspaceRepo.rows.append(workspace); workspaces::WorkspaceValidator validator;
        shared::identifiers::UuidIdGenerator ids; shared::time::SystemApplicationClock clock;
        workspaces::WorkspaceManager manager(workspaceRepo, validator, ids, clock, accountId);
        workspaces::WorkspaceContext workspaceContext; workspaceContext.activate(workspace);
        ServiceRepo servicesRepo; ProcessLauncher launcher; services::ServiceCoordinator coordinator(servicesRepo, launcher);
        NotesRepo noteRepo; tasks::TaskScheduler scheduler(ids); FakeToolClient client;
        AssistantController controller(client, clock, ids, workspaceRoot.path(), {}, QProcessEnvironment{});
        controller.applyConfiguration({QStringLiteral("codex.exe"), {}}); QSignalSpy probe(&controller, &AssistantController::executableProbeRequested);
        controller.open(); controller.setPersistenceContext(accountId.toString(), false);
        controller.setContext({workspaceId.toString(), workspace.displayName(), root});
        controller.executableFound({QStringLiteral("C:/fixture/codex.exe"), QStringLiteral("0.160.1")}, probe.front().at(1).toULongLong());
        AssistantToolRuntime runtime(client, controller, scheduler, manager, workspaceContext, servicesRepo, coordinator, noteRepo);
        const QString relative = QStringLiteral("src/readme.txt");
        QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("src"))));
        QFile file(QDir(root).filePath(relative)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("safe UTF-8 text"); file.close();
        QFile largeFile(QDir(root).filePath(QStringLiteral("src/large.txt")));
        QVERIFY(largeFile.open(QIODevice::WriteOnly)); largeFile.write(QByteArray(64 * 1024, 'x')); largeFile.close();

        client.call(1, QStringLiteral("devhub_files_read"), {{QStringLiteral("path"), relative}});
        QCOMPARE(client.replies.size(), 1);
        QCOMPARE(responseBody(client.replies).value(QStringLiteral("data")).toObject().value(QStringLiteral("error")).toString(), QStringLiteral("scope_denied"));
        controller.setToolDisclosureGrants(false, true);
        client.call(2, QStringLiteral("devhub_files_read"), {{QStringLiteral("path"), relative},
            {QStringLiteral("offset"), 0}, {QStringLiteral("length"), 64}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 2, 3000);
        auto body = responseBody(client.replies);
        QVERIFY(body.value(QStringLiteral("success")).toBool());
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("safe UTF-8 text"));

        client.call(4, QStringLiteral("devhub_files_read"), {{QStringLiteral("path"), QStringLiteral("src/large.txt")},
            {QStringLiteral("offset"), 0}, {QStringLiteral("length"), 64 * 1024}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 3, 3000);
        body = responseBody(client.replies, 2);
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("text")).toString().size(), 24 * 1024);
        QVERIFY(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("truncated")).toBool());

        client.call(3, QStringLiteral("devhub_files_search"), {{QStringLiteral("query"), QStringLiteral("readme")}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 4, 3000);
        const auto paths = responseBody(client.replies, 3).value(QStringLiteral("data")).toObject().value(QStringLiteral("paths")).toArray();
        QCOMPARE(paths.size(), 1); QCOMPARE(paths.first().toString(), relative);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QStringLiteral("Aé🙂中Z").toUtf8()); file.close();
        client.call(5, QStringLiteral("devhub_files_read"), {{QStringLiteral("path"), relative},
            {QStringLiteral("offset"), 3}, {QStringLiteral("length"), 4}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 5, 3000);
        body = responseBody(client.replies, 4);
        QVERIFY(body.value(QStringLiteral("success")).toBool());
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("🙂"));
        client.call(6, QStringLiteral("devhub_files_read"), {{QStringLiteral("path"), relative},
            {QStringLiteral("offset"), 1e30}});
        QCOMPARE(client.replies.size(), 6);
        body = responseBody(client.replies, 5);
        QVERIFY(!body.value(QStringLiteral("success")).toBool());
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("error")).toString(), QStringLiteral("invalid_arguments"));
    }

    void gitStatusUsesExistingServiceAndRejectsParentRepositoryExpansion()
    {
        const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
        if (git.isEmpty()) QSKIP("Git is not installed; the production Git tool remains fail-closed.");
        QTemporaryDir workspaceRoot(QDir::current().filePath(QStringLiteral("phase8-git-XXXXXX")));
        QVERIFY(workspaceRoot.isValid());
        QProcess init;
        init.setProgram(git); init.setArguments({QStringLiteral("init"), QStringLiteral("--quiet")});
        init.setWorkingDirectory(workspaceRoot.path()); init.start();
        QVERIFY(init.waitForStarted(3000)); QVERIFY(init.waitForFinished(10000)); QCOMPARE(init.exitCode(), 0);
        const auto gitCommand = [&git, &workspaceRoot](QStringList arguments) {
            QProcess process; process.setProgram(git); process.setArguments(std::move(arguments));
            process.setWorkingDirectory(workspaceRoot.path()); process.start();
            return process.waitForStarted(3000) && process.waitForFinished(10000) && process.exitCode() == 0;
        };
        QVERIFY(gitCommand({QStringLiteral("-c"), QStringLiteral("user.name=DevHub test"),
            QStringLiteral("-c"), QStringLiteral("user.email=devhub-test@example.invalid"),
            QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("first"), QStringLiteral("--quiet")}));
        QVERIFY(gitCommand({QStringLiteral("-c"), QStringLiteral("user.name=DevHub test"),
            QStringLiteral("-c"), QStringLiteral("user.email=devhub-test@example.invalid"),
            QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("second"), QStringLiteral("--quiet")}));

        const auto accountId = domain::account::AccountId::fromString(QStringLiteral("92f343d4-5b1e-41ac-9c83-99f6410e1b3f")).value();
        const auto workspaceId = domain::workspace::WorkspaceId::fromString(QStringLiteral("8a0d03aa-6cd0-46b6-941b-2b362053d97f")).value();
        const QString root = QFileInfo(workspaceRoot.path()).canonicalFilePath();
        const auto workspace = domain::workspace::Workspace(workspaceId, accountId, QStringLiteral("Git"),
            {workspaceRoot.path(), root}, domain::workspace::WorkspaceStatus::Ready, false, 0, QDateTime::currentDateTimeUtc());
        WorkspaceRepo workspaceRepo; workspaceRepo.rows.append(workspace); workspaces::WorkspaceValidator validator;
        shared::identifiers::UuidIdGenerator ids; shared::time::SystemApplicationClock clock;
        workspaces::WorkspaceManager manager(workspaceRepo, validator, ids, clock, accountId);
        workspaces::WorkspaceContext workspaceContext; workspaceContext.activate(workspace);
        ServiceRepo serviceRepo; ProcessLauncher launcher; services::ServiceCoordinator coordinator(serviceRepo, launcher);
        NotesRepo noteRepo; tasks::TaskScheduler scheduler(ids); FakeToolClient client;
        AssistantController controller(client, clock, ids, workspaceRoot.path(), {}, QProcessEnvironment{});
        controller.applyConfiguration({QStringLiteral("codex.exe"), {}}); QSignalSpy probe(&controller, &AssistantController::executableProbeRequested);
        controller.open(); controller.setPersistenceContext(accountId.toString(), false);
        controller.setContext({workspaceId.toString(), workspace.displayName(), root});
        controller.executableFound({QStringLiteral("C:/fixture/codex.exe"), QStringLiteral("0.160.1")}, probe.front().at(1).toULongLong());
        AssistantToolRuntime runtime(client, controller, scheduler, manager, workspaceContext, serviceRepo, coordinator, noteRepo);

        application::git::GitExecutableLocator locator;
        const auto executable = locator.locate(); QVERIFY(executable);
        application::git::GitService service(application::git::GitCommandRunner(executable.value().path));
        const auto expected = service.status(root); QVERIFY(expected); QVERIFY(expected.value().isRepository);
        client.call(1, QStringLiteral("devhub_git_status"));
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 1, 5000);
        auto body = responseBody(client.replies);
        QVERIFY(body.value(QStringLiteral("success")).toBool());
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("isRepository")).toBool(), expected.value().isRepository);
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("branch")).toString(), expected.value().branch);
        const auto expectedBranches = service.branches(root, false); QVERIFY(expectedBranches);
        client.call(2, QStringLiteral("devhub_git_branches"), {{QStringLiteral("remote"), false}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 2, 5000);
        QCOMPARE(responseBody(client.replies, 1).value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("branches")).toArray().size(), expectedBranches.value().size());
        const auto expectedHistory = service.history(root, 0, 50); QVERIFY(expectedHistory);
        client.call(3, QStringLiteral("devhub_git_log"));
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 3, 5000);
        body = responseBody(client.replies, 2);
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("limit")).toInt(), 50);
        QCOMPARE(body.value(QStringLiteral("data")).toObject().value(QStringLiteral("commits")).toArray().size(), expectedHistory.value().size());
        client.call(4, QStringLiteral("devhub_git_log"), {{QStringLiteral("limit"), 100}});
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 4, 5000);
        QCOMPARE(responseBody(client.replies, 3).value(QStringLiteral("data")).toObject().value(QStringLiteral("limit")).toInt(), 100);

        QTemporaryDir parentRepo(QDir::current().filePath(QStringLiteral("phase8-parent-git-XXXXXX")));
        QVERIFY(parentRepo.isValid());
        QProcess parentInit; parentInit.setProgram(git); parentInit.setArguments({QStringLiteral("init"), QStringLiteral("--quiet")});
        parentInit.setWorkingDirectory(parentRepo.path()); parentInit.start();
        QVERIFY(parentInit.waitForStarted(3000)); QVERIFY(parentInit.waitForFinished(10000)); QCOMPARE(parentInit.exitCode(), 0);
        const QString nested = QDir(parentRepo.path()).filePath(QStringLiteral("selected")); QVERIFY(QDir().mkpath(nested));
        const QString nestedCanonical = QFileInfo(nested).canonicalFilePath();
        const auto nestedWorkspaceId = domain::workspace::WorkspaceId::fromString(QStringLiteral("f93e9980-e56d-4e64-b855-5fe97977d0e1")).value();
        const auto nestedWorkspace = domain::workspace::Workspace(nestedWorkspaceId, accountId, QStringLiteral("Nested"),
            {nested, nestedCanonical}, domain::workspace::WorkspaceStatus::Ready, false, 0, QDateTime::currentDateTimeUtc());
        workspaceContext.activate(nestedWorkspace);
        controller.setContext({nestedWorkspaceId.toString(), nestedWorkspace.displayName(), nestedCanonical});
        client.call(5, QStringLiteral("devhub_git_status"));
        QTRY_COMPARE_WITH_TIMEOUT(client.replies.size(), 5, 5000);
        body = responseBody(client.replies, 4);
        QVERIFY(body.value(QStringLiteral("success")).toBool());
        QVERIFY(!body.value(QStringLiteral("data")).toObject().value(QStringLiteral("isRepository")).toBool());
    }
};

QTEST_GUILESS_MAIN(AssistantToolRuntimeTest)
#include "assistant_tool_runtime_test.moc"
