// Opt-in developer probe. Never reads/copies the user's Codex home or runs a turn.
#include "infrastructure/codex/CodexExecutableLocator.h"
#include "infrastructure/codex/CodexProcess.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
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
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
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
    int stoppedCount = 0;
    QObject::connect(&sample, &QTimer::timeout, &app, observe);
    QObject::connect(&locator, &CodexExecutableLocator::probeFailed, &app, [&](const auto&, quint64) { failed = true; app.exit(4); });
    QObject::connect(&locator, &CodexExecutableLocator::executableFound, &app, [&](const CodexExecutable& executable, quint64) {
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
    const QJsonObject evidence{{"candidateVersion", "0.160.1"}, {"isolatedHome", true}, {"inference", false}, {"scenario", mode},
        {"initialized", initialized}, {"configRead", configRead}, {"observedOwnedProcessCount", owned.size()},
        {"survivingOwnedProcessCount", survivors}, {"stoppedCount", stoppedCount}, {"transportFailed", failed},
        {"treeContainmentClaim", false}};
    const auto bytes = QJsonDocument(evidence).toJson(QJsonDocument::Indented);
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    return code || failed != expectedFailure || !initialized || !configRead || stoppedCount != 1 || survivors ? 1 : 0;
}
