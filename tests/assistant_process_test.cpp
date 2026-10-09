#include "infrastructure/codex/CodexProcess.h"
#include "infrastructure/codex/CodexExecutableLocator.h"
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace devhub::application::assistant;
using devhub::infrastructure::codex::CodexProcess;
using devhub::infrastructure::codex::CodexExecutableLocator;
namespace {
bool alive(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!handle) return false;
    const bool running = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    CloseHandle(handle); return running;
#else
    Q_UNUSED(pid); return false;
#endif
}
CodexTransportLimits fastLimits()
{
    CodexTransportLimits limits; limits.drainMilliseconds = 100; limits.terminateMilliseconds = 100;
    return limits;
}
CodexLaunchSpec spec(const QString& scratch, const QString& scenario, quint64 generation = 7)
{
    return {{QStringLiteral(DEVHUB_FAKE_CODEX), QStringLiteral("0.160.1")}, {scenario}, scratch,
        codexBaseEnvironment(QProcessEnvironment::systemEnvironment()), generation};
}
}
class AssistantProcessTest final : public QObject
{
    Q_OBJECT
    QByteArray m_previousPath;
private slots:
    void initTestCase()
    {
        // The fake Qt executable is also copied to disposable locations for
        // locator tests. Give children an explicit DLL search path for this kit.
        m_previousPath = qgetenv("PATH");
        qputenv("PATH", QDir::toNativeSeparators(QLibraryInfo::path(QLibraryInfo::BinariesPath)).toLocal8Bit()
            + ';' + m_previousPath);
    }
    void cleanupTestCase() { qputenv("PATH", m_previousPath); }
    void fragmentedAndEcho()
    {
        QTemporaryDir scratch; QVERIFY(scratch.isValid());
        CodexProcess process(nullptr, fastLimits());
        QSignalSpy started(&process, &ICodexTransport::started), received(&process, &ICodexTransport::objectReceived),
            stopped(&process, &ICodexTransport::stopped), failed(&process, &ICodexTransport::failed);
        process.launch(spec(scratch.path(), "fragment"));
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(received.front().at(0).toJsonObject().value("params").toObject().value("text").toString(), QString::fromUtf8("İstanbul 🐈"));
        const auto pid = process.processId(); QVERIFY(alive(pid)); process.stop(); QTRY_COMPARE(stopped.size(), 1);
        QVERIFY(!alive(pid)); QCOMPARE(failed.size(), 0); QCOMPARE(started.size(), 1);
        process.launch(spec(scratch.path(), "echo", 8)); QTRY_COMPARE(started.size(), 2);
        QVERIFY(process.sendObject({{"id", "same-id"}, {"method", "echo"}, {"params", QJsonObject{{"ok", true}}}}));
        QTRY_COMPARE(received.size(), 2);
        QCOMPARE(received.back().at(0).toJsonObject().value("id").toString(), QStringLiteral("same-id"));
        QCOMPARE(received.back().at(1).toULongLong(), quint64(8));
        process.stop(); QTRY_COMPARE(stopped.size(), 2); QCOMPARE(failed.size(), 0);
    }
    void boundedOutput_data()
    {
        QTest::addColumn<QString>("scenario"); QTest::addColumn<int>("count");
        QTest::newRow("stderr flood") << QStringLiteral("stderr") << 1;
        QTest::newRow("event burst") << QStringLiteral("burst") << 5000;
        QTest::newRow("large history") << QStringLiteral("large") << 1;
    }
    void boundedOutput()
    {
        QFETCH(QString, scenario); QFETCH(int, count);
        QTemporaryDir scratch; CodexProcess process(nullptr, fastLimits());
        QSignalSpy received(&process, &ICodexTransport::objectReceived), stopped(&process, &ICodexTransport::stopped),
            failed(&process, &ICodexTransport::failed);
        int heartbeats = 0;
        QTimer heartbeat; heartbeat.setInterval(0);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeats; }); heartbeat.start();
        process.launch(spec(scratch.path(), scenario)); QTRY_COMPARE_WITH_TIMEOUT(received.size(), count, 10000);
        QVERIFY(heartbeats > 2);
        if (scenario == QStringLiteral("stderr")) {
            QTRY_VERIFY(process.diagnosticPreview().contains("healthy"));
            QVERIFY(process.diagnosticPreview().size() <= 64 * 1024);
            QVERIFY(!process.diagnosticPreview().contains("secret-value"));
            QVERIFY(process.diagnosticPreview().contains("[REDACTED]"));
        }
        process.stop(); QTRY_COMPARE(stopped.size(), 1); QCOMPARE(failed.size(), 0);
    }
    void faults_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const auto& value : {"malformed", "oversize", "truncated", "crash"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void faults()
    {
        QFETCH(QString, scenario); QTemporaryDir scratch;
        CodexProcess process(nullptr, fastLimits());
        QSignalSpy started(&process, &ICodexTransport::started), stopped(&process, &ICodexTransport::stopped),
            failed(&process, &ICodexTransport::failed);
        process.launch(spec(scratch.path(), scenario)); QTRY_COMPARE(started.size(), 1);
        const auto pid = process.processId();
        QTRY_COMPARE(stopped.size(), 1); QCOMPARE(failed.size(), 1);
        if (pid) QVERIFY(!alive(pid));
        QCOMPARE(stopped.front().at(2).toULongLong(), quint64(7));
        process.launch(spec(scratch.path(), "echo", 9)); QTRY_COMPARE(started.size(), 2);
        process.stop(); QTRY_COMPARE(stopped.size(), 2); QCOMPARE(failed.size(), 1);
    }
    void finalFrameBeforeExit()
    {
        QTemporaryDir scratch; CodexProcess process;
        QSignalSpy received(&process, &ICodexTransport::objectReceived), stopped(&process, &ICodexTransport::stopped),
            failed(&process, &ICodexTransport::failed);
        process.launch(spec(scratch.path(), "exitburst")); QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(received.size(), 5000); QCOMPARE(failed.size(), 0);
    }
    void stopDrainsAcceptedWrites()
    {
        QTemporaryDir scratch; CodexProcess process;
        QSignalSpy started(&process, &ICodexTransport::started), received(&process, &ICodexTransport::objectReceived),
            stopped(&process, &ICodexTransport::stopped), failed(&process, &ICodexTransport::failed);
        process.launch(spec(scratch.path(), "echo")); QTRY_COMPARE(started.size(), 1);
        for (int id = 1; id <= 2; ++id) {
            QVERIFY(process.sendObject({{"id", id}, {"method", "echo"},
                {"params", QJsonObject{{"text", QString(100 * 1024, 'x')}}}}));
        }
        process.stop(); QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(received.size(), 2); QCOMPARE(failed.size(), 0);
        QCOMPARE(received.back().at(0).toJsonObject().value("result").toObject().value("text").toString().size(), 100 * 1024);
    }
    void writePressureAndHungStop()
    {
        QTemporaryDir scratch; auto limits = fastLimits(); limits.stdinBytes = 256 * 1024;
        CodexProcess process(nullptr, limits);
        QSignalSpy started(&process, &ICodexTransport::started), stopped(&process, &ICodexTransport::stopped);
        process.launch(spec(scratch.path(), "pressure")); QTRY_COMPARE(started.size(), 1);
        const auto pid = process.processId();
        const QJsonObject request{{"id", 1}, {"method", "read"}, {"params", QJsonObject{{"text", QString(100 * 1024, 'x')}}}};
        QVERIFY(process.sendObject(request)); QVERIFY(process.sendObject(request));
        QVERIFY(!process.sendObject(request)); QVERIFY(process.pendingWriteBytes() <= limits.stdinBytes);
        process.stop(); process.stop(); QVERIFY(!process.sendObject(request));
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 3000); QVERIFY(!alive(pid));
    }
    void oneByteWritesDrainBeforeEof()
    {
        QTemporaryDir scratch; CodexTransportLimits limits; limits.writeChunkBytes = 1;
        CodexProcess process(nullptr, limits);
        QSignalSpy started(&process, &ICodexTransport::started), received(&process, &ICodexTransport::objectReceived),
            stopped(&process, &ICodexTransport::stopped), failed(&process, &ICodexTransport::failed);
        process.launch(spec(scratch.path(), "echo")); QTRY_COMPARE(started.size(), 1);
        const QJsonObject first{{"id", 1}, {"method", "echo"},
            {"params", QJsonObject{{"text", QString::fromUtf8("İ 🐈\nfirst")}}}};
        const QJsonObject second{{"id", "two"}, {"method", "echo"}, {"params", QJsonObject{{"text", "second"}}}};
        QVERIFY(process.sendObject(first)); QVERIFY(process.sendObject(second));
        QVERIFY(process.pendingWriteBytes() > 1); // A suffix remains after the forced one-byte writes.
        process.stop(); QVERIFY(!process.sendObject(first));
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 5000);
        QCOMPARE(failed.size(), 0); QCOMPARE(received.size(), 2); QCOMPARE(process.pendingWriteBytes(), qint64(0));
        const auto replyOne = received.front().at(0).toJsonObject();
        const auto replyTwo = received.back().at(0).toJsonObject();
        QCOMPARE(replyOne.value("id"), first.value("id")); QCOMPARE(replyOne.value("result"), first.value("params"));
        QCOMPARE(replyTwo.value("id"), second.value("id")); QCOMPARE(replyTwo.value("result"), second.value("params"));
    }
    void ownedHelperCleanup()
    {
        QTemporaryDir scratch; CodexProcess process(nullptr, fastLimits());
        QSignalSpy received(&process, &ICodexTransport::objectReceived), stopped(&process, &ICodexTransport::stopped);
        process.launch(spec(scratch.path(), "owned")); QTRY_COMPARE(received.size(), 1);
        const auto childPid = received.front().at(0).toJsonObject().value("params").toObject().value("pid").toInteger();
        const auto parentPid = process.processId(); QVERIFY(alive(childPid));
        process.stop(); QTRY_COMPARE(stopped.size(), 1);
        QVERIFY(!alive(parentPid)); QTRY_VERIFY(!alive(childPid));
    }
    void stopDuringStartup()
    {
        QTemporaryDir scratch; CodexProcess process(nullptr, fastLimits());
        QSignalSpy stopped(&process, &ICodexTransport::stopped), failed(&process, &ICodexTransport::failed);
        process.launch(spec(scratch.path(), "hang")); process.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 3000);
        QCOMPARE(failed.size(), 0); QCOMPARE(process.processId(), qint64(0));
    }
    void startupFailure()
    {
        QTemporaryDir scratch; const auto path = scratch.filePath("invalid.exe");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not a PE executable"); file.close();
        const CodexLaunchSpec launch{{path, QStringLiteral("0.160.1")}, {QStringLiteral("echo")},
            scratch.path(), codexBaseEnvironment(QProcessEnvironment::systemEnvironment()), 7};
        CodexProcess process; QSignalSpy failed(&process, &ICodexTransport::failed), stopped(&process, &ICodexTransport::stopped);
        process.launch(launch); QTRY_COMPARE(stopped.size(), 1); QCOMPARE(failed.size(), 1);
        const CodexLaunchSpec invalidDirectory{launch.executable, launch.arguments, QStringLiteral("relative"),
            launch.environment, launch.processGeneration};
        process.launch(invalidDirectory); QTRY_COMPARE(stopped.size(), 2); QCOMPARE(failed.size(), 2);
    }
    void locatorValidationAndPath()
    {
        QTemporaryDir scratch; QVERIFY(QDir().mkpath(scratch.filePath(QString::fromUtf8("native İ path"))));
        const auto path = scratch.filePath(QString::fromUtf8("native İ path/codex.exe"));
        QVERIFY(QFile::copy(QStringLiteral(DEVHUB_FAKE_CODEX), path));
        CodexExecutableLocator locator;
        QSignalSpy found(&locator, &CodexExecutableLocator::executableFound), failed(&locator, &CodexExecutableLocator::probeFailed);
        locator.locate(path, 11); QTRY_COMPARE(found.size(), 1);
        const auto value = qvariant_cast<CodexExecutable>(found.front().at(0));
        QCOMPARE(value.version, QStringLiteral("0.160.1")); QCOMPARE(found.front().at(1).toULongLong(), quint64(11));
        locator.locate(scratch.filePath("missing.exe"), 12); QCOMPARE(failed.size(), 1);
        locator.locate(scratch.path(), 13); QCOMPARE(failed.size(), 2);
        const auto wrapper = scratch.filePath("codex.cmd"); QVERIFY(QFile::copy(QStringLiteral(DEVHUB_FAKE_CODEX), wrapper));
        locator.locate(wrapper, 14); QCOMPARE(failed.size(), 3);
        const auto previousPath = qgetenv("PATH");
        qputenv("PATH", QFileInfo(path).absolutePath().toLocal8Bit() + ';' + previousPath);
        locator.locate({}, 15);
        qputenv("PATH", previousPath); // findExecutable has already resolved it synchronously.
        QTRY_COMPARE(found.size(), 2);
    }
    void locatorFaults_data()
    {
        QTest::addColumn<QString>("name"); QTest::addColumn<bool>("cancel");
        QTest::newRow("unrecognized version") << QStringLiteral("unknown") << false;
        QTest::newRow("malformed version") << QStringLiteral("badversion") << false;
        QTest::newRow("oversized version") << QStringLiteral("oversize") << false;
        QTest::newRow("timeout") << QStringLiteral("hang") << false;
        QTest::newRow("cancel") << QStringLiteral("hang") << true;
    }
    void locatorFaults()
    {
        QFETCH(QString, name); QFETCH(bool, cancel); QTemporaryDir scratch;
        const auto path = scratch.filePath(name + ".exe"); QVERIFY(QFile::copy(QStringLiteral(DEVHUB_FAKE_CODEX), path));
        CodexExecutableLocator locator(nullptr, 200);
        QSignalSpy found(&locator, &CodexExecutableLocator::executableFound), failed(&locator, &CodexExecutableLocator::probeFailed);
        locator.locate(path, 42); if (cancel) locator.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000); QTRY_VERIFY(!locator.isProbing());
        QCOMPARE(found.size(), 0); QCOMPARE(failed.front().at(1).toULongLong(), quint64(42));
        locator.locate(QStringLiteral(DEVHUB_FAKE_CODEX), 43); QTRY_COMPARE(found.size(), 1);
    }
    void applicationShutdownWatchdog_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("async stop then quit") << QStringLiteral("normal");
        QTest::newRow("emergency destructor") << QStringLiteral("emergency");
    }
    void applicationShutdownWatchdog()
    {
        QFETCH(QString, mode);
        QProcess driver; driver.setProgram(QStringLiteral(DEVHUB_SHUTDOWN_DRIVER)); driver.setArguments({mode}); driver.start();
        QSignalSpy finished(&driver, qOverload<int, QProcess::ExitStatus>(&QProcess::finished));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        QCOMPARE(driver.exitCode(), 0); QCOMPARE(driver.exitStatus(), QProcess::NormalExit);
        bool valid = false; const auto pid = driver.readAllStandardOutput().trimmed().toLongLong(&valid);
        QVERIFY(valid); QVERIFY(pid > 0); QVERIFY(!alive(pid));
    }
};
QTEST_GUILESS_MAIN(AssistantProcessTest)
#include "assistant_process_test.moc"
