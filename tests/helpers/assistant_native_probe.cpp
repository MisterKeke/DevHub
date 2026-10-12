// Opt-in native probes. Containment modes use an isolated home; shared-readiness
// checks account/config/catalog through the runtime only. No mode sends a turn.
#include "infrastructure/codex/CodexExecutableLocator.h"
#include "infrastructure/codex/CodexProcess.h"
#include "application/assistant/AssistantToolRuntime.h"
#include "application/assistant/AssistantController.h"
#include "infrastructure/codex/CodexClient.h"
#include "infrastructure/codex/CodexRuntimeProfile.h"
#include "../support/AssistantHarness.h"
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QHash>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

using namespace devhub::application::assistant;
using namespace devhub::infrastructure::codex;
// Explicit read-only readiness check. The runtime owns credential handling;
// this helper never sends a prompt, starts login, or reads credential files.
static int checkSharedReadiness(QCoreApplication& app, const QString& executablePath, int reconnects = 0,
    const QString& metadataPath = {}, const QString& existingScratch = {}, bool newChatReconnect = false)
{
    QTemporaryDir scratch; if (!scratch.isValid()) return 3;
    devhub::tests::assistant::MemoryConversationRepository repository;
    if (!metadataPath.isEmpty()) {
        QFile metadataFile(metadataPath); if (!metadataFile.open(QIODevice::ReadOnly)) return 3;
        const auto value = QJsonDocument::fromJson(metadataFile.readAll()).object();
        AssistantConversationMetadata metadata;
        metadata.devhubAccountId = value.value("accountId").toString();
        metadata.conversation.id = QUuid(value.value("id").toString());
        metadata.conversation.context = {value.value("workspaceId").toString(), "Native restore check", value.value("workspaceRoot").toString()};
        metadata.binding = {value.value("threadId").toString(), value.value("accountKey").toString(), {},
            value.value("homeKey").toString(), existingScratch, value.value("profile").toString()};
        metadata.createdAtUtc = QDateTime::fromString(value.value("createdAt").toString(), Qt::ISODate);
        metadata.lastTurnId = value.value("lastTurnId").toString();
        metadata.lastTurnStatus = value.value("lastTurnStatus").toString();
        if (metadata.conversation.id.isNull() || metadata.devhubAccountId.isEmpty() || metadata.binding.threadId.isEmpty()
            || !QFileInfo(existingScratch).isDir()) return 3;
        repository.rows.append(metadata);
    }
    CodexProcess process; CodexClient client(process); CodexExecutableLocator locator;
    devhub::shared::time::SystemApplicationClock clock;
    devhub::shared::identifiers::UuidIdGenerator ids;
    if (!client.configureDynamicTools(AssistantToolRuntime::experimentalToolSpecifications())) return 3;
    AssistantController controller(client, clock, ids, existingScratch.isEmpty() ? scratch.path() : existingScratch,
        CodexRuntimeProfile::launchArguments().value(), codexBaseEnvironment(QProcessEnvironment::systemEnvironment()),
        metadataPath.isEmpty() ? nullptr : &repository);
    if (!metadataPath.isEmpty()) {
        controller.setContext(repository.rows.first().conversation.context);
        controller.setPersistenceContext(repository.rows.first().devhubAccountId, true);
    }
    controller.applyConfiguration({executablePath, {}});
    QString version; bool finishing = false; int launches = 0; int completedReconnects = 0;
    bool reconnectQueued = false;
    QString transportFailure, protocolFailure; QStringList requestFailures, profileChecks;
    bool restored = false; int restorations = 0;
    qsizetype initialMessageCount = -1;
    QJsonObject resumeScope;
    QObject::connect(&process, &ICodexTransport::objectReceived, &controller, [&](const QJsonObject& object, quint64) {
        const auto result = object.value("result").toObject();
        if (!result.contains("thread")) return;
        const auto thread = result.value("thread").toObject();
        QJsonArray environments;
        for (const auto& entry : thread.value("environments").toArray()) {
            const auto environment = entry.toObject();
            environments.append(QJsonObject{{"scratchMatches", QFileInfo(environment.value("cwd").toString()).canonicalFilePath()
                .compare(QFileInfo(existingScratch).canonicalFilePath(), Qt::CaseInsensitive) == 0},
                {"runtimeRootCount", environment.value("runtimeWorkspaceRoots").toArray().size()}});
        }
        QJsonArray turns;
        for (const auto& entry : thread.value("turns").toArray()) {
            const auto turn = entry.toObject(); QJsonArray items;
            for (const auto& item : turn.value("items").toArray()) items.append(item.toObject().value("type"));
            turns.append(QJsonObject{{"status", turn.value("status")}, {"itemsView", turn.value("itemsView")}, {"itemTypes", items}});
        }
        resumeScope = {{"historyMode", thread.value("historyMode")}, {"environmentsNull", thread.value("environments").isNull()},
            {"environmentCount", thread.value("environments").toArray().size()}, {"environments", environments}, {"turns", turns},
            {"approvalPolicy", result.value("approvalPolicy")}, {"sandbox", result.value("sandbox")},
            {"runtimeRootCount", result.value("runtimeWorkspaceRoots").toArray().size()},
            {"initialTurnsPageNull", result.value("initialTurnsPage").isNull()},
            {"initialPageTurnCount", result.value("initialTurnsPage").toObject().value("data").toArray().size()},
            {"initialPageNextCursor", !result.value("initialTurnsPage").toObject().value("nextCursor").toString().isEmpty()},
            {"initialPageBackwardsCursor", !result.value("initialTurnsPage").toObject().value("backwardsCursor").toString().isEmpty()},
            {"turnCursorPresent", !result.value("turnsBackwardsCursor").toString().isEmpty()},
            {"itemCursorPresent", !result.value("itemsBackwardsCursor").toString().isEmpty()}};
    });
    QObject::connect(&process, &ICodexTransport::failed, &controller, [&](const auto& error, quint64) { transportFailure = error.userMessage; });
    QObject::connect(&client, &ICodexClient::protocolFailed, &controller, [&](const auto& error) { protocolFailure = error.userMessage; });
    QObject::connect(&client, &ICodexClient::requestFailed, &controller, [&](qint64, const QString& method, const auto& error) { requestFailures.append(method + ": " + error.userMessage); });
    QObject::connect(&client, &ICodexClient::profileChecked, &controller, [&](bool accepted, const QStringList& reasons) { profileChecks.append(accepted ? "verified" : reasons.join(',')); });
    QObject::connect(&controller, &AssistantController::executableProbeRequested, &locator,
        [&](const QString& path, quint64 generation) { ++launches; locator.locate(path, generation); });
    QObject::connect(&controller, &AssistantController::cancelProbeRequested, &locator, &CodexExecutableLocator::cancel);
    QObject::connect(&locator, &CodexExecutableLocator::executableFound, &controller,
        [&](const CodexExecutable& executable, quint64 generation) { version = executable.version; controller.executableFound(executable, generation); });
    QObject::connect(&locator, &CodexExecutableLocator::probeFailed, &controller, &AssistantController::probeFailed);
    auto finish = [&](bool ready) {
        if (finishing) return;
        finishing = true;
        const auto account = client.account();
        bool textModel = false;
        for (const auto& model : controller.models()) textModel |= !model.hidden && model.supportsText && model.availabilityMessage.isEmpty();
        const QJsonObject evidence{{"codexVersion", version}, {"sharedProfile", true}, {"launchCount", launches},
            {"completedReconnects", completedReconnects}, {"restored", restored}, {"restorationCount", restorations}, {"resumeScope", resumeScope},
            {"newChatSelected", newChatReconnect && controller.messages().isEmpty()
                && controller.conversation().id != repository.rows.first().conversation.id},
            {"profileVerified", client.profileVerified()}, {"chatGptAccount", account.mode == CodexAccountMode::ChatGpt},
            {"accountRoutingVerified", !account.identityFingerprint.isEmpty()}, {"textModelAvailable", textModel},
            {"canSend", controller.canSend()}, {"status", controller.statusText()},
            {"transportFailure", transportFailure}, {"protocolFailure", protocolFailure},
            {"requestFailures", QJsonArray::fromStringList(requestFailures)}, {"profileChecks", QJsonArray::fromStringList(profileChecks)},
            {"inference", false}, {"loginRequested", false}, {"configurationWriteRequested", false}};
        const auto bytes = QJsonDocument(evidence).toJson(QJsonDocument::Indented);
        std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
        controller.beginShutdown([&, ready] { app.exit(ready ? 0 : 1); });
    };
    auto advance = [&] {
        if (controller.canSend() && (metadataPath.isEmpty() || restored) && !reconnectQueued && !finishing) {
            reconnectQueued = true;
            QTimer::singleShot(0, &controller, [&] {
                reconnectQueued = false;
                if (!controller.canSend() || (!metadataPath.isEmpty() && !restored)) return;
                if (completedReconnects < reconnects) {
                    initialMessageCount = controller.messages().size();
                    ++completedReconnects;
                    if (newChatReconnect) controller.newChat();
                    controller.reconnect();
                } else finish(newChatReconnect ? (restorations == 1 && controller.messages().isEmpty()
                    && controller.conversation().id != repository.rows.first().conversation.id)
                    : metadataPath.isEmpty() || !reconnects || (restorations == reconnects + 1
                        && controller.messages().size() == initialMessageCount
                        && controller.conversation().id == repository.rows.first().conversation.id));
            });
        }
    };
    QObject::connect(&controller, &AssistantController::stateChanged, &controller, advance);
    QObject::connect(&client, &ICodexClient::threadReceived, &controller, [&](const CodexThread&) {
        restored = true; ++restorations; advance();
    });
    QTimer::singleShot(0, &controller, &AssistantController::open);
    QTimer::singleShot(reconnects ? 55000 : 20000, &controller, [&] { finish(false); });
    QTimer::singleShot(reconnects ? 60000 : 30000, &app, [&] { app.exit(5); });
    return app.exec();
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == QStringLiteral("--check-shared-restoration")
        || app.arguments().value(1) == QStringLiteral("--check-shared-conversation-reconnect")
        || app.arguments().value(1) == QStringLiteral("--check-shared-new-chat")) {
        if (app.arguments().size() != 5) return 2;
        const bool newChat = app.arguments().at(1) == QStringLiteral("--check-shared-new-chat");
        return checkSharedReadiness(app, app.arguments().at(2), newChat || app.arguments().at(1) == QStringLiteral("--check-shared-conversation-reconnect") ? 1 : 0,
            app.arguments().at(3), app.arguments().at(4), newChat);
    }
    if (app.arguments().value(1) == QStringLiteral("--check-shared-readiness")
        || app.arguments().value(1) == QStringLiteral("--check-shared-reconnect")) {
        if (app.arguments().size() != 3) return 2;
        return checkSharedReadiness(app, app.arguments().at(2),
            app.arguments().at(1) == QStringLiteral("--check-shared-reconnect") ? 3 : 0);
    }
    if (app.arguments().value(1) == QStringLiteral("--tool-specs")) {
        if (app.arguments().size() != 3) return 2;
        QFile file(app.arguments().at(2)); if (!file.open(QIODevice::WriteOnly)) return 3;
        const auto bytes = QJsonDocument(AssistantToolRuntime::experimentalToolSpecifications()).toJson();
        return file.write(bytes) == bytes.size() ? 0 : 4;
    }
    if (app.arguments().size() < 2 || app.arguments().size() > 3) return 2;
    const auto mode = app.arguments().value(2, QStringLiteral("stop"));
    if (mode != QStringLiteral("stop") && mode != QStringLiteral("crash") && mode != QStringLiteral("kill")) return 2;
    QTemporaryDir root;
    if (!root.isValid()) return 3;
    QDir().mkpath(root.filePath("scratch")); QDir().mkpath(root.filePath("codex-home"));
    CodexTransportLimits limits;
    if (mode == QStringLiteral("kill")) { limits.drainMilliseconds = 0; limits.terminateMilliseconds = 0; }
    CodexProcess process(nullptr, limits);
    CodexExecutableLocator locator;
    QTimer sample;
    sample.setInterval(5);
    QSet<qint64> owned;
#ifdef Q_OS_WIN
    QHash<qint64, HANDLE> handles;
    auto observe = [&] {
        const auto pid = process.processId();
        if (pid) owned.insert(pid);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        QList<QPair<qint64, qint64>> entries;
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) do {
            entries.append({entry.th32ProcessID, entry.th32ParentProcessID});
        } while (Process32NextW(snapshot, &entry));
        CloseHandle(snapshot);
        bool added;
        do {
            added = false;
            for (const auto& pair : entries) if (owned.contains(pair.second) && !owned.contains(pair.first)) {
                owned.insert(pair.first); added = true;
            }
        } while (added);
        // Hold handles to prevent PID reuse until the test completes.
        for (const auto value : owned) {
            if (handles.contains(value)) continue;
            HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(value));
            if (handle) handles.insert(value, handle);
        }
    };
#else
    auto observe = [&] { if (process.processId()) owned.insert(process.processId()); };
#endif
    bool initialized = false, configRead = false, failed = false;
    QString candidateVersion;
    int stoppedCount = 0;
    QObject::connect(&sample, &QTimer::timeout, &app, observe);
    QObject::connect(&locator, &CodexExecutableLocator::probeFailed, &app, [&](const auto&, quint64) { failed = true; app.exit(4); });
    QObject::connect(&locator, &CodexExecutableLocator::executableFound, &app, [&](const CodexExecutable& executable, quint64) {
        candidateVersion = executable.version;
        QStringList args{QStringLiteral("app-server"), QStringLiteral("--listen"), QStringLiteral("stdio://"), QStringLiteral("--strict-config")};
        const QStringList overrides = {"features.shell_tool=false", "features.unified_exec=false", "web_search=\"disabled\"",
            "sandbox_mode=\"read-only\"", "approval_policy=\"on-request\"", "forced_login_method=\"chatgpt\"",
            "features.apps=false", "features.plugins=false", "features.hooks=false", "features.browser_use=false",
            "features.computer_use=false", "features.multi_agent=false", "features.js_repl=false",
            "features.apply_patch_freeform=false", "features.shell_snapshot=false", "features.goals=false", "features.image_generation=false"};
        for (const auto& value : overrides) args.append({QStringLiteral("-c"), value});
        auto env = codexBaseEnvironment(QProcessEnvironment::systemEnvironment());
        env.insert("CODEX_HOME", root.filePath("codex-home")); // Test-only disposable home.
        process.launch({executable, args, root.filePath("scratch"), env, 1});
    });
    QObject::connect(&process, &ICodexTransport::started, &app, [&](quint64) {
        observe(); sample.start();
        if (!process.sendObject({{"id", 1}, {"method", "initialize"}, {"params", QJsonObject{
            {"clientInfo", QJsonObject{{"name", "devhub_phase1_probe"}, {"version", "0.0.1"}}}}}})) {
            failed = true; process.stop();
        }
    });
    QObject::connect(&process, &ICodexTransport::objectReceived, &app, [&](const QJsonObject& object, quint64) {
        if (object.contains("method")) {
            if (object.contains("id")) {
                const auto sent = process.sendObject({{"id", object.value("id")}, {"error", QJsonObject{
                    {"code", -32601}, {"message", "Disabled by transport probe"}}}});
                if (!sent) failed = true;
            }
            return;
        }
        if (object.contains("error")) { failed = true; process.stop(); return; }
        if (object.value("id").toInt() == 1) {
            initialized = true;
            if (!process.sendObject({{"method", "initialized"}})
                || !process.sendObject({{"id", 2}, {"method", "config/read"}, {"params", QJsonObject{{"includeLayers", false}}}})) {
                failed = true; process.stop();
            }
        } else if (object.value("id").toInt() == 2) {
            configRead = true; observe();
#ifdef Q_OS_WIN
            if (mode == QStringLiteral("crash")) {
                HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(process.processId()));
                if (!handle || !TerminateProcess(handle, 42)) { failed = true; process.stop(); }
                if (handle) CloseHandle(handle);
            } else process.stop();
#else
            process.stop();
#endif
        }
    });
    QObject::connect(&process, &ICodexTransport::failed, &app, [&](const auto&, quint64) { failed = true; });
    QObject::connect(&process, &ICodexTransport::stopped, &app, [&](int, bool, quint64) {
        ++stoppedCount; sample.stop();
        // Allow already-exiting descendants a bounded observation window.
        QTimer::singleShot(100, &app, [&] { app.quit(); });
    });
    QTimer::singleShot(0, &app, [&] { locator.locate(app.arguments().at(1), 1); });
    QTimer::singleShot(15000, &app, [&] { failed = true; process.stop(); });
    QTimer::singleShot(20000, &app, [&] { app.exit(5); });
    const int code = app.exec();
    int survivors = 0;
#ifdef Q_OS_WIN
    QSet<DWORD> liveIds;
    for (const auto handle : handles) {
        if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) liveIds.insert(GetProcessId(handle));
        CloseHandle(handle);
    }
    survivors = static_cast<int>(liveIds.size());
#endif
    const bool expectedFailure = mode == QStringLiteral("crash");
    const QJsonObject evidence{{"candidateVersion", candidateVersion}, {"isolatedHome", true}, {"loginRequested", false},
        {"inference", false}, {"scenario", mode},
        {"initialized", initialized}, {"configRead", configRead}, {"observedOwnedProcessCount", owned.size()},
        {"survivingOwnedProcessCount", survivors}, {"stoppedCount", stoppedCount}, {"transportFailed", failed},
        {"treeContainmentClaim", false}};
    const auto bytes = QJsonDocument(evidence).toJson(QJsonDocument::Indented);
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    return code || failed != expectedFailure || !initialized || !configRead || stoppedCount != 1 || survivors ? 1 : 0;
}
