#include "../src/application/assistant/AssistantToolRegistry.h"
#include "../src/application/assistant/AssistantApprovalCoordinator.h"
#include "../src/application/assistant/AssistantToolHandlers.h"
#include "../src/application/files/WorkspaceFileService.h"
#include "../src/application/notes/NoteSearchService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QtTest>

using namespace devhub::application::assistant;
using namespace devhub::application::files;

namespace {
template<typename T> devhub::shared::common::Result<T> failureResult()
{
    return devhub::shared::common::Result<T>::failure(devhub::domain::errors::AppError::failure(
        devhub::domain::errors::ErrorCode::IoFailure, devhub::domain::errors::ErrorCategory::FileSystem,
        QStringLiteral("Unavailable"), {}, QStringLiteral("test")));
}
class NotesSpy final : public devhub::application::notes::INotesRepository {
public:
    int boundedReads = 0;
    QString workspace;
    QString id;
    qint64 cap = 0;
    devhub::shared::common::Result<QVector<devhub::application::notes::NoteNode>> list(const QString&, bool) override { return devhub::shared::common::Result<QVector<devhub::application::notes::NoteNode>>::success({}); }
    devhub::shared::common::Result<devhub::application::notes::NoteNode> create(const QString&, QString, QString, devhub::application::notes::NoteNodeKind) override { return failureResult<devhub::application::notes::NoteNode>(); }
    devhub::shared::common::Result<void> rename(const QString&, const QString&, QString) override { return devhub::shared::common::Result<void>::success(); }
    devhub::shared::common::Result<void> move(const QString&, const QString&, QString) override { return devhub::shared::common::Result<void>::success(); }
    devhub::shared::common::Result<void> setPinned(const QString&, const QString&, bool) override { return devhub::shared::common::Result<void>::success(); }
    devhub::shared::common::Result<void> setTags(const QString&, const QString&, QStringList) override { return devhub::shared::common::Result<void>::success(); }
    devhub::shared::common::Result<QString> readContent(const QString&, const QString&) override { return failureResult<QString>(); }
    devhub::shared::common::Result<QString> readContentBounded(const QString& ws, const QString& noteId, qint64 maximum) override {
        ++boundedReads; workspace = ws; id = noteId; cap = maximum;
        return devhub::shared::common::Result<QString>::success(QStringLiteral("authorized text"));
    }
    devhub::shared::common::Result<QString> saveContent(const QString&, const QString&, const QString&, const QString&) override { return failureResult<QString>(); }
    QString contentPath(const QString&, const QString&) const override { return {}; }
    devhub::shared::common::Result<void> setTrashed(const QString&, const QString&, bool) override { return devhub::shared::common::Result<void>::success(); }
    devhub::shared::common::Result<void> permanentlyRemove(const QString&, const QString&) override { return devhub::shared::common::Result<void>::success(); }
};
bool writeFile(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
ToolScope authorizedScope(QString scope = QStringLiteral("scope-a"), quint64 access = 4, quint64 process = 7)
{
    return ToolScope(std::move(scope), QStringLiteral("account-internal"), QStringLiteral("workspace-a"),
                     QStringLiteral("C:/private/workspace"), access, process, true);
}
}

class AssistantToolsTest final : public QObject
{
    Q_OBJECT
private slots:
    void approvalExpiryToleratesReentrantRevocation() {
        AssistantApprovalCoordinator coordinator; int decisions = 0;
        ToolCall first; first.callId = "first"; first.name = "approved_edit"; first.expiresAtMonotonicMs = 100000;
        auto second = first; second.callId = "second";
        const auto a = coordinator.request(first, {}, "Workspace", "Edit", {}, [&](bool allow) {
            QVERIFY(!allow); ++decisions; coordinator.revokeAll();
        });
        const auto b = coordinator.request(second, {}, "Workspace", "Edit", {}, [&](bool allow) { QVERIFY(!allow); ++decisions; });
        coordinator.expireNow(100001); QCOMPARE(decisions, 2); QVERIFY(coordinator.pending().isEmpty());
        QVERIFY(!coordinator.decide(a, true)); QVERIFY(!coordinator.decide(b, true));
    }
    void mutationsKeepOutcomesBeyondReadCacheAndNeverReplayCancelledWork() {
        QHash<QString, ToolResult> results; int mutations = 0;
        AssistantToolRegistry registry([&](const QString& route, const ToolResult& result) { results.insert(route, result); });
        ToolDefinition write; write.name = "approved_edit"; write.description = "Approved edit"; write.effect = ToolEffect::ApprovedMutation;
        std::function<void(ToolResult)> pending;
        QVERIFY(registry.registerTool(write, [&](const ToolCall&, const ToolCancellation& cancellation, auto done) {
            ++mutations; cancellation.markMutationStarted(); pending = std::move(done);
        }));
        QSignalSpy uncertain(&registry, &AssistantToolRegistry::mutationUncertain);
        ToolCall call{"write-route", "write-call", "approved_edit", {}, authorizedScope(), 100000};
        QVERIFY(registry.invoke(call)); registry.cancelRoute(call.routeId); QCOMPARE(mutations, 1); QCOMPARE(uncertain.size(), 1);
        QCOMPARE(results.value("write-route").data.value("outcome").toString(), QString("uncertain"));
        ToolResult late; late.success = true; pending(late); QVERIFY(!results.value("write-route").success);
        ToolDefinition read; read.name = "read_meta"; read.description = "Read metadata";
        QVERIFY(registry.registerTool(read, [](const ToolCall&, const ToolCancellation&, auto done) { ToolResult r; r.success = true; done(r); }));
        for (int i = 0; i < 600; ++i) {
            const auto id = QString::number(i);
            QVERIFY(registry.invoke({"read-route-" + id, "read-call-" + id, "read_meta", {}, authorizedScope(), 100000}));
        }
        call.routeId = "after-cache-eviction"; QVERIFY(registry.invoke(call)); QCOMPARE(mutations, 1);
        QCOMPARE(results.value(call.routeId).data, results.value("write-route").data);
    }

    void synchronousScopeRevocationPreventsHandlerEntry() {
        int invoked = 0; AssistantToolRegistry registry([](const QString&, const ToolResult&) {});
        ToolDefinition def; def.name = "read_meta"; def.description = "Read metadata";
        QVERIFY(registry.registerTool(def, [&](const ToolCall&, const ToolCancellation&, auto) { ++invoked; }));
        connect(&registry, &AssistantToolRegistry::activityStarted, &registry, [&] { registry.cancelAll(); });
        QVERIFY(registry.invoke({"route", "call", def.name, {}, authorizedScope(), 100000})); QCOMPARE(invoked, 0);
    }
    void registryStrictArgumentsAndReadOnlyAdmission()
    {
        QStringList resolved;
        AssistantToolRegistry registry([&](const QString& route, const ToolResult& result) {
            resolved.append(route + QLatin1Char(':') + result.errorCode);
        });
        int calls = 0;
        ToolDefinition definition;
        definition.name = QStringLiteral("files_read"); definition.description = QStringLiteral("Read a bounded file range.");
        definition.allowedArguments = {QStringLiteral("path"), QStringLiteral("offset")};
        definition.requiredArguments = {QStringLiteral("path")};
        definition.validateArguments = [](const QJsonObject& input, QJsonObject* normalized) {
            if (!input.value(QStringLiteral("path")).isString()) return false;
            if (input.contains(QStringLiteral("offset")) && (!input.value(QStringLiteral("offset")).isDouble()
                || input.value(QStringLiteral("offset")).toDouble() < 0)) return false;
            *normalized = input;
            return true;
        };
        QVERIFY(registry.registerTool(definition, [&](const ToolCall& call, const ToolCancellation&, auto done) {
            ++calls;
            QVERIFY(call.arguments.value(QStringLiteral("path")).isString());
            ToolResult result; result.success = true; result.data.insert(QStringLiteral("text"), QStringLiteral("safe")); done(result);
        }));
        QVERIFY(!registry.registerTool(definition, {}));
        ToolDefinition mutation; mutation.name = QStringLiteral("mutate"); mutation.description = QStringLiteral("Mutation");
        mutation.effect = ToolEffect::UnsupportedMutation;
        QVERIFY(!registry.registerTool(mutation, [](const ToolCall&, const ToolCancellation&, auto) {}));
        ToolDefinition tooSmall; tooSmall.name = QStringLiteral("too_small");
        tooSmall.description = QStringLiteral("Cannot fit bounded errors."); tooSmall.maximumOutputBytes = 64;
        QVERIFY(!registry.registerTool(tooSmall, [](const ToolCall&, const ToolCancellation&, auto) {}));
        ToolCall valid{QStringLiteral("route-1"), QStringLiteral("call-1"), QStringLiteral("files_read"),
            {{QStringLiteral("path"), QStringLiteral("src/main.cpp")}, {QStringLiteral("offset"), 2}}, authorizedScope(), 0};
        QVERIFY(registry.invoke(valid));
        QVERIFY2(calls == 1, qPrintable(resolved.isEmpty() ? QStringLiteral("handler was not invoked") : resolved.last()));
        QTRY_COMPARE(resolved.size(), 1);
        QVERIFY(registry.invoke(valid));
        QCOMPARE(resolved.size(), 1);
        ToolCall invalid = valid; invalid.routeId = QStringLiteral("route-2"); invalid.callId = QStringLiteral("call-2");
        invalid.arguments.insert(QStringLiteral("accountId"), QStringLiteral("forged"));
        QVERIFY(registry.invoke(invalid));
        QCOMPARE(resolved.last(), QStringLiteral("route-2:invalid_arguments"));
        invalid.routeId = QStringLiteral("route-3"); invalid.arguments = {{QStringLiteral("path"), 12}};
        QVERIFY(registry.invoke(invalid));
        QCOMPARE(resolved.last(), QStringLiteral("route-3:invalid_arguments"));
        invalid.routeId = QStringLiteral("route-4"); invalid.arguments = {{QStringLiteral("path"), QStringLiteral("x")}, {QStringLiteral("root"), QStringLiteral("C:/")}};
        QVERIFY(registry.invoke(invalid));
        QCOMPARE(resolved.last(), QStringLiteral("route-4:invalid_arguments"));
        QCOMPARE(calls, 1);
        ToolCall parallel = valid; parallel.callId = QStringLiteral("call-other");
        QCOMPARE(AssistantToolRegistry::schedulerModuleKey(valid) == AssistantToolRegistry::schedulerModuleKey(parallel), false);
    }

    void registryDuplicateExpiryCancellationAndStaleCompletion()
    {
        QHash<QString, ToolResult> resolved;
        AssistantToolRegistry registry([&](const QString& route, const ToolResult& result) { resolved.insert(route, result); }, 2, 8);
        QSignalSpy finished(&registry, &AssistantToolRegistry::activityFinished);
        std::function<void(ToolResult)> pending;
        ToolCancellation workerCancellation;
        ToolDefinition def; def.name = QStringLiteral("notes_search"); def.description = QStringLiteral("Search authorized notes.");
        def.allowedArguments = {QStringLiteral("query")}; def.requiredArguments = {QStringLiteral("query")};
        def.validateArguments = [](const QJsonObject& input, QJsonObject* normalized) {
            if (!input.value(QStringLiteral("query")).isString() || input.value(QStringLiteral("query")).toString().isEmpty()) return false;
            *normalized = input; return true;
        };
        QVERIFY(registry.registerTool(def, [&](const ToolCall&, const ToolCancellation& cancellation, auto done) {
            workerCancellation = cancellation; pending = std::move(done);
        }));
        ToolCall call{QStringLiteral("r1"), QStringLiteral("same-call"), QStringLiteral("notes_search"),
            {{QStringLiteral("query"), QStringLiteral("needle")}}, authorizedScope(), 100000};
        QVERIFY(registry.invoke(call));
        ToolCall duplicate = call; duplicate.routeId = QStringLiteral("r2");
        QVERIFY(registry.invoke(duplicate));
        QCOMPARE(registry.activeCount(), 1);
        QCOMPARE(resolved.size(), 0);
        registry.cancelScope(QStringLiteral("scope-a"), 4);
        QCOMPARE(resolved.size(), 2);
        QVERIFY(workerCancellation.isCancellationRequested());
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.first().at(2).toBool());
        auto cancelledAlias = call; cancelledAlias.routeId = QStringLiteral("cancelled-alias");
        QVERIFY(registry.invoke(cancelledAlias));
        QCOMPARE(resolved.value(QStringLiteral("cancelled-alias")).errorCode, QStringLiteral("scope_revoked"));
        QCOMPARE(registry.activeCount(), 0);
        resolved.remove(QStringLiteral("cancelled-alias"));

        ToolCall processCall{QStringLiteral("r-process"), QStringLiteral("process-call"), QStringLiteral("notes_search"),
            {{QStringLiteral("query"), QStringLiteral("x")}}, authorizedScope(QStringLiteral("scope-b"), 5, 9), 100000};
        QVERIFY(registry.invoke(processCall));
        registry.cancelProcess(9);
        QCOMPARE(resolved.value(QStringLiteral("r-process")).errorCode, QStringLiteral("process_lost"));
        QCOMPARE(resolved.size(), 3);
        QCOMPARE(registry.activeCount(), 0);
        ToolResult late; late.success = true; late.data.insert(QStringLiteral("body"), QStringLiteral("late")); pending(late);
        QCoreApplication::processEvents();
        QCOMPARE(resolved.size(), 3);

        ToolCall expiring{QStringLiteral("r3"), QStringLiteral("exp-call"), QStringLiteral("notes_search"),
            {{QStringLiteral("query"), QStringLiteral("x")}}, authorizedScope(), 5};
        QVERIFY(registry.invoke(expiring));
        registry.expireNow(5);
        QVERIFY(resolved.contains(QStringLiteral("r3")));
        QCOMPARE(resolved.value(QStringLiteral("r3")).errorCode, QStringLiteral("expired"));
        QVERIFY(workerCancellation.isCancellationRequested());

        QHash<QString, ToolResult> busyResults;
        AssistantToolRegistry boundedRegistry([&](const QString& route, const ToolResult& result) { busyResults.insert(route, result); }, 1, 2);
        ToolDefinition boundedDef; boundedDef.name = QStringLiteral("bounded"); boundedDef.description = QStringLiteral("Bounded concurrent read.");
        std::function<void(ToolResult)> heldCompletion;
        QVERIFY(boundedRegistry.registerTool(boundedDef, [&](const ToolCall&, const ToolCancellation&, auto complete) {
            heldCompletion = std::move(complete);
        }));
        ToolCall admitted{QStringLiteral("held"), QStringLiteral("held-call"), QStringLiteral("bounded"), {}, authorizedScope(), 0};
        QVERIFY(boundedRegistry.invoke(admitted));
        ToolCall alias = admitted; alias.routeId = QStringLiteral("held-alias");
        QVERIFY(boundedRegistry.invoke(alias));
        ToolCall aliasOverLimit = admitted; aliasOverLimit.routeId = QStringLiteral("held-alias-over-limit");
        QVERIFY(boundedRegistry.invoke(aliasOverLimit));
        QCOMPARE(busyResults.value(QStringLiteral("held-alias-over-limit")).errorCode, QStringLiteral("busy"));
        ToolCall rejected{QStringLiteral("busy"), QStringLiteral("busy-call"), QStringLiteral("bounded"), {}, authorizedScope(), 0};
        QVERIFY(boundedRegistry.invoke(rejected));
        QCOMPARE(busyResults.value(QStringLiteral("busy")).errorCode, QStringLiteral("busy"));
        ToolResult done; done.success = true; heldCompletion(done);
        QTRY_VERIFY(busyResults.contains(QStringLiteral("held")));
        QTRY_VERIFY(busyResults.contains(QStringLiteral("held-alias")));
    }

    void identicalCallIdsInDifferentScopesKeepIndependentResults()
    {
        QHash<QString, ToolResult> resolved;
        AssistantToolRegistry registry([&](const QString& route, const ToolResult& result) { resolved.insert(route, result); });
        QList<std::function<void(ToolResult)>> callbacks;
        ToolDefinition definition; definition.name = QStringLiteral("scoped_read"); definition.description = QStringLiteral("Scoped read.");
        QVERIFY(registry.registerTool(definition, [&](const ToolCall&, const ToolCancellation&, auto done) { callbacks.append(std::move(done)); }));
        ToolCall first{QStringLiteral("scope-a-route"), QStringLiteral("same-id"), definition.name, {}, authorizedScope(QStringLiteral("scope-a")), 0};
        ToolCall second = first; second.routeId = QStringLiteral("scope-b-route"); second.scope = authorizedScope(QStringLiteral("scope-b"));
        QVERIFY(registry.invoke(first)); QVERIFY(registry.invoke(second)); QCOMPARE(callbacks.size(), 2);
        ToolResult firstResult; firstResult.success = true; firstResult.data.insert(QStringLiteral("body"), QStringLiteral("scope-a-only"));
        callbacks.first()(firstResult); QTRY_COMPARE(resolved.size(), 1);
        QVERIFY(!resolved.contains(second.routeId)); QCOMPARE(registry.activeCount(), 1);
        ToolResult secondResult; secondResult.success = true; secondResult.data.insert(QStringLiteral("body"), QStringLiteral("scope-b-only"));
        callbacks.last()(secondResult); QTRY_COMPARE(resolved.size(), 2);
        QCOMPARE(resolved.value(first.routeId).data, firstResult.data);
        QCOMPARE(resolved.value(second.routeId).data, secondResult.data);
    }

    void registryDeduplicatesCompletedCallAndCapsOutput()
    {
        QHash<QString, ToolResult> resolved;
        AssistantToolRegistry registry([&](const QString& route, const ToolResult& result) { resolved.insert(route, result); });
        int calls = 0;
        ToolDefinition def; def.name = QStringLiteral("bounded"); def.description = QStringLiteral("Bounded read.");
        def.maximumOutputBytes = 128;
        def.allowedArguments = {QStringLiteral("tag")};
        def.validateArguments = [](const QJsonObject& input, QJsonObject* normalized) {
            if (input.contains(QStringLiteral("tag")) && !input.value(QStringLiteral("tag")).isString()) return false;
            *normalized = input; return true;
        };
        QVERIFY(registry.registerTool(def, [&](const ToolCall&, const ToolCancellation&, auto done) {
            ++calls; ToolResult result; result.success = true; result.data.insert(QStringLiteral("text"), QString(1000, QLatin1Char('x'))); done(result);
        }));
        ToolCall first{QStringLiteral("r1"), QStringLiteral("dedup"), QStringLiteral("bounded"), {}, authorizedScope(), 0};
        QVERIFY(registry.invoke(first)); QTRY_VERIFY(resolved.contains(QStringLiteral("r1")));
        QVERIFY(resolved.value(QStringLiteral("r1")).truncated);
        QVERIFY(resolved.value(QStringLiteral("r1")).compactJson().size() <= 128);
        ToolCall second = first; second.routeId = QStringLiteral("r2");
        QVERIFY(registry.invoke(second));
        QCOMPARE(calls, 1);
        QVERIFY(resolved.contains(QStringLiteral("r2")));
        ToolCall conflicting = first; conflicting.routeId = QStringLiteral("r3");
        conflicting.arguments.insert(QStringLiteral("tag"), QStringLiteral("different request"));
        QVERIFY(registry.invoke(conflicting));
        QCOMPARE(calls, 1);
        QCOMPARE(resolved.value(QStringLiteral("r3")).errorCode, QStringLiteral("duplicate_conflict"));
    }

    void boundedNoteHandlerUsesRepositoryOwnershipBoundaryOnce()
    {
        NotesSpy repository;
        QHash<QString, ToolResult> resolved;
        AssistantToolRegistry registry([&](const QString& route, const ToolResult& result) { resolved.insert(route, result); });
        QVERIFY(registerBoundedNoteReadTool(registry, repository));
        ToolCall call{QStringLiteral("note-route"), QStringLiteral("note-call"), QStringLiteral("notes_read"),
            {{QStringLiteral("noteId"), QStringLiteral("09a97323-0c21-44bd-988e-288dc642735a")}},
            ToolScope(QStringLiteral("scope-a"), QStringLiteral("account-internal"), QStringLiteral("workspace-a"),
                QStringLiteral("C:/private/workspace"), 4, 7, true,
                {QStringLiteral("09a97323-0c21-44bd-988e-288dc642735a")}), 0};
        QVERIFY(registry.invoke(call)); QTRY_VERIFY(resolved.contains(QStringLiteral("note-route")));
        QCOMPARE(repository.boundedReads, 1);
        QCOMPARE(repository.workspace, QStringLiteral("workspace-a"));
        QCOMPARE(repository.id, QStringLiteral("09a97323-0c21-44bd-988e-288dc642735a"));
        QCOMPARE(repository.cap, qint64(2 * 1024 * 1024));
        QVERIFY(resolved.value(QStringLiteral("note-route")).success);
        ToolCall undisclosed = call; undisclosed.routeId = QStringLiteral("undisclosed-route"); undisclosed.callId = QStringLiteral("undisclosed-call");
        undisclosed.scope = authorizedScope();
        QVERIFY(registry.invoke(undisclosed));
        QCOMPARE(resolved.value(QStringLiteral("undisclosed-route")).errorCode, QStringLiteral("scope_denied"));
        QCOMPARE(repository.boundedReads, 1);
        ToolCall forged = call; forged.routeId = QStringLiteral("forged-route"); forged.callId = QStringLiteral("forged-call");
        forged.arguments.insert(QStringLiteral("account_id"), QStringLiteral("another-account"));
        QVERIFY(registry.invoke(forged));
        QCOMPARE(resolved.value(QStringLiteral("forged-route")).errorCode, QStringLiteral("invalid_arguments"));
        QCOMPARE(repository.boundedReads, 1);
    }

    void destroyedRegistryDropsLateWorkerCallback()
    {
        std::function<void(ToolResult)> pending;
        auto registry = std::make_unique<AssistantToolRegistry>([](const QString&, const ToolResult&) {});
        ToolDefinition definition; definition.name = QStringLiteral("slow"); definition.description = QStringLiteral("Slow fake operation.");
        QVERIFY(registry->registerTool(definition, [&](const ToolCall&, const ToolCancellation&, auto done) { pending = std::move(done); }));
        ToolCall call{QStringLiteral("slow-route"), QStringLiteral("slow-call"), QStringLiteral("slow"), {}, authorizedScope(), 0};
        QVERIFY(registry->invoke(call));
        registry.reset();
        ToolResult late; late.success = true;
        pending(late);
        QCoreApplication::processEvents();
    }

    void filePolicyRejectsTraversalAdsAndSecrets()
    {
        QTemporaryDir temp(QDir::current().filePath(QStringLiteral("phase7-tools-XXXXXX"))); QVERIFY(temp.isValid());
        WorkspaceFilePolicy policy(temp.path());
        QString error;
        QVERIFY(!policy.resolveReadPath(QStringLiteral("../outside"), &error));
        QVERIFY(!policy.resolveReadPath(QStringLiteral("folder/../../outside"), &error));
        QVERIFY(!policy.resolveReadPath(QStringLiteral("file.txt:secret"), &error));
        QVERIFY(!policy.resolveReadPath(QStringLiteral(".ssh/id_ed25519"), &error));
        QVERIFY(!policy.resolveReadPath(QStringLiteral(".env.local"), &error));
        QVERIFY(!policy.resolveReadPath(QStringLiteral("C:/outside"), &error));
        QVERIFY(policy.resolveReadPath(QStringLiteral("safe.txt"), &error).has_value());
        QVERIFY(!policy.isInsideWorkspace(QDir(temp.path()).filePath(QStringLiteral("../lookalike")), false));
    }

    void filePreviewUtf8BomBinaryLimitAndRanges()
    {
        QTemporaryDir temp(QDir::current().filePath(QStringLiteral("phase7-tools-XXXXXX"))); QVERIFY(temp.isValid());
        QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral("bom.txt")), QByteArray::fromHex("efbbbf") + QStringLiteral("héllo").toUtf8()));
        QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral("binary.bin")), QByteArray("a\0b", 3)));
        QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral("large.txt")), QByteArray(1024 * 1024 + 1, 'a')));
        WorkspaceFileService service{WorkspaceFilePolicy(temp.path())};
        const auto preview = service.readPreview(QStringLiteral("bom.txt")); QVERIFY(preview.readable); QCOMPARE(preview.text, QStringLiteral("héllo"));
        QVERIFY(service.readPreview(QStringLiteral("binary.bin")).binary);
        QVERIFY(service.readPreview(QStringLiteral("large.txt")).tooLarge);
        const auto range = service.readRange(QStringLiteral("bom.txt"), 0, 5, 3); QCOMPARE(range.bytes.size(), 3); QVERIFY(range.truncated);
        QVERIFY(service.readRange(QStringLiteral("bom.txt"), -1, 1).bytes.isEmpty());
    }

    void filenameSearchBoundsPrunesSecretsAndCancels()
    {
        QTemporaryDir temp(QDir::current().filePath(QStringLiteral("phase7-tools-XXXXXX"))); QVERIFY(temp.isValid());
        QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral("src/needle.cpp")), "source"));
        QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral(".ssh/needle-secret")), "private"));
        QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral(".git/needle-object")), "object"));
        WorkspaceFileService service{WorkspaceFilePolicy(temp.path())};
        const auto found = service.searchNames(QStringLiteral("needle"));
        QCOMPARE(found.relativePaths, QStringList{QStringLiteral("src/needle.cpp")});
        QVERIFY(service.searchNames(QStringLiteral("src/needle.cpp")).relativePaths.contains(QStringLiteral("src/needle.cpp")));
        for (int index = 0; index < 105; ++index) {
            const QString name = QStringLiteral("many/needle-%1.txt").arg(index, 3, 10, QLatin1Char('0'));
            QVERIFY(writeFile(QDir(temp.path()).filePath(name), "x"));
        }
        WorkspaceFileSearchLimits raised;
        raised.maximumResults = 10000; raised.maximumEntries = 100000; raised.maximumDepth = 100;
        raised.maximumMilliseconds = 10000;
        QCOMPARE(service.searchNames(QStringLiteral("needle"), {}, raised).relativePaths.size(), 100);
        WorkspaceFileSearchLimits limits; limits.maximumEntries = 1;
        const auto bounded = service.searchNames(QStringLiteral("missing"), {}, limits);
        QVERIFY(bounded.entriesVisited <= 1);
        auto cancellation = std::make_shared<std::atomic_bool>(true);
        QVERIFY(service.searchNames(QStringLiteral("needle"), cancellation).cancelled);
    }

    void workspaceLinksCannotExpandSearchOrReadScope()
    {
        QTemporaryDir root(QDir::current().filePath(QStringLiteral("phase7-tools-XXXXXX")));
        QTemporaryDir external(QDir::current().filePath(QStringLiteral("phase7-outside-XXXXXX")));
        QVERIFY(root.isValid() && external.isValid());
        QVERIFY(QDir().mkpath(QDir(external.path()).filePath(QStringLiteral("nested"))));
        const QString secret = QDir(external.path()).filePath(QStringLiteral("nested/needle-outside.txt"));
        QVERIFY(writeFile(secret, "outside"));
        const QString link = QDir(root.path()).filePath(QStringLiteral("linked-folder"));
        QProcess createJunction;
        const QString command = QStringLiteral("mklink /J %1 %2")
            .arg(QDir::toNativeSeparators(link), QDir::toNativeSeparators(external.path()));
        createJunction.start(QStringLiteral("cmd.exe"), {QStringLiteral("/d"), QStringLiteral("/c"), command});
        const bool started = createJunction.waitForStarted(2000);
        const bool finished = started && createJunction.waitForFinished(5000);
        const bool junction = QFileInfo(link).isJunction();
        if (!finished || createJunction.exitCode() != 0 || !junction)
            QSKIP(qPrintable(QStringLiteral("Temporary junction unavailable (started=%1 finished=%2 exit=%3 detected=%4).")
                .arg(started).arg(finished).arg(createJunction.exitCode()).arg(junction)));
        WorkspaceFilePolicy policy(root.path());
        QVERIFY(!policy.resolveReadPath(QStringLiteral("linked-folder/nested/needle-outside.txt")));
        WorkspaceFileService service(policy);
        QVERIFY(service.searchNames(QStringLiteral("needle")).relativePaths.isEmpty());
        const auto notePath = QDir(link).filePath(QStringLiteral("nested/needle-outside.txt"));
        QVERIFY(devhub::application::notes::NoteSearchService().search({{QStringLiteral("outside-note"), notePath}}, QStringLiteral("outside")).noteIds.isEmpty());
        QVERIFY(QDir().rmdir(link));
    }

    void noteSearchHasSourceCapAndCancellation()
    {
        QTemporaryDir temp(QDir::current().filePath(QStringLiteral("phase7-tools-XXXXXX"))); QVERIFY(temp.isValid());
        QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral("ok.md")), QStringLiteral("prefix Needle suffix").toUtf8()));
        QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral("large.md")), QByteArray(2 * 1024 * 1024 + 1, 'x')));
        const QList<devhub::application::notes::AuthorizedNoteSource> sources{{QStringLiteral("ok"), QDir(temp.path()).filePath(QStringLiteral("ok.md"))},
            {QStringLiteral("large"), QDir(temp.path()).filePath(QStringLiteral("large.md"))}};
        const auto result = devhub::application::notes::NoteSearchService().search(sources, QStringLiteral("needle"));
        QCOMPARE(result.noteIds, QStringList{QStringLiteral("ok")}); QCOMPARE(result.filesRead, 1);
        auto cancellation = std::make_shared<std::atomic_bool>(true);
        QVERIFY(devhub::application::notes::NoteSearchService().search(sources, QStringLiteral("needle"), cancellation).cancelled);
    }
};

QTEST_GUILESS_MAIN(AssistantToolsTest)
#include "assistant_tools_test.moc"
