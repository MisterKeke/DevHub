#include "../src/application/buildtest/BuildExecutionCoordinator.h"
#include "../src/application/buildtest/BuildResultParser.h"
#include "../src/application/buildtest/BuildSystemDetector.h"
#include "../src/infrastructure/buildtest/SqlBuildHistoryRepository.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/application/workspaces/WorkspaceContext.h"
#include "../src/domain/account/AccountId.h"
#include "../src/domain/workspace/Workspace.h"
#include "../src/domain/workspace/WorkspaceId.h"
#include "../src/shared/security/RedactionService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>

#include <algorithm>

using namespace devhub;
using namespace devhub::application;
using namespace devhub::application::buildtest;

namespace {

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}

class MemoryHistory final : public IBuildHistoryRepository
{
public:
    shared::common::Result<QVector<BuildProfile>> profiles(const QString& workspaceId) override
    {
        QVector<BuildProfile> result;
        for (const auto& profile : m_profiles) if (profile.workspaceId == workspaceId) result.append(profile);
        return shared::common::Result<QVector<BuildProfile>>::success(std::move(result));
    }
    shared::common::Result<void> saveProfile(const BuildProfile& profile) override
    {
        for (auto& current : m_profiles) if (current.id == profile.id) { current = profile; return shared::common::Result<void>::success(); }
        m_profiles.append(profile); return shared::common::Result<void>::success();
    }
    shared::common::Result<void> setProfileTrusted(const QString& workspaceId, const QString& profileId, bool trusted) override
    {
        for (auto& profile : m_profiles) if (profile.workspaceId == workspaceId && profile.id == profileId) {
            profile.trusted = trusted; return shared::common::Result<void>::success();
        }
        return shared::common::Result<void>::success();
    }
    shared::common::Result<void> removeProfile(const QString&, const QString&) override
    { return shared::common::Result<void>::success(); }
    shared::common::Result<void> startRun(const BuildRun& run) override
    { m_runs.insert(run.id, run); return shared::common::Result<void>::success(); }
    shared::common::Result<void> finishRun(const BuildRun& run) override
    { m_runs.insert(run.id, run); return shared::common::Result<void>::success(); }
    shared::common::Result<QVector<BuildRun>> recentRuns(const QString&, int) override
    { return shared::common::Result<QVector<BuildRun>>::success(m_runs.values().toVector()); }
    shared::common::Result<QVector<TestResult>> testResults(const QString&, const QString&) override
    { return shared::common::Result<QVector<TestResult>>::success({}); }
private:
    QVector<BuildProfile> m_profiles;
    QHash<QString, BuildRun> m_runs;
};

class FakeProcess final : public services::IManagedProcess
{
public:
    enum class FinishMode { Success, Failure, Wait };
    FakeProcess(FinishMode mode, QByteArray output, QObject* parent)
        : IManagedProcess(parent), m_mode(mode), m_output(std::move(output)) {}
    void start() override
    {
        QTimer::singleShot(0, this, [this] {
            emit started();
            emit outputAvailable(m_output, false);
            if (m_mode != FinishMode::Wait) QTimer::singleShot(10, this, [this] { complete(); });
        });
    }
    void stop(int) override { complete(); }
    qint64 processId() const override { return 1234; }
private:
    void complete()
    {
        if (m_finished) return;
        m_finished = true;
        emit finished(m_mode == FinishMode::Success ? 0 : 1,
                      m_mode == FinishMode::Success ? static_cast<int>(QProcess::NormalExit)
                                                    : static_cast<int>(QProcess::CrashExit));
    }
    FinishMode m_mode;
    QByteArray m_output;
    bool m_finished = false;
};

class FakeLauncher final : public services::IProcessLauncher
{
public:
    FakeProcess::FinishMode mode = FakeProcess::FinishMode::Success;
    QByteArray output = QByteArrayLiteral("fake output\n");
    QString program;
    QStringList arguments;
    QString workingDirectory;
    services::IManagedProcess* launch(const domain::services::ServiceDefinition& definition, QObject* parent) override
    {
        program = definition.program; arguments = definition.arguments; workingDirectory = definition.workingDirectory;
        return new FakeProcess(mode, output, parent);
    }
};

domain::workspace::Workspace makeWorkspace(const QString& root, const QString& workspaceId,
                                           const QString& accountId)
{
    return domain::workspace::Workspace(
        domain::workspace::WorkspaceId::fromString(workspaceId).value(),
        domain::account::AccountId::fromString(accountId).value(), QStringLiteral("Project"),
        {root, QFileInfo(root).canonicalFilePath()}, domain::workspace::WorkspaceStatus::Ready,
        false, 0, QDateTime::currentDateTimeUtc());
}

} // namespace

class BuildTestTest final : public QObject
{
    Q_OBJECT
private slots:
    void detectsProjectMarkersAndPresetSemantics()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("CMakeLists.txt")), "cmake_minimum_required(VERSION 3.28)\n"));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("CMakePresets.json")), R"({"version":6,"configurePresets":[{"name":"base","hidden":true,"generator":"Ninja","binaryDir":"${sourceDir}/out/${presetName}"},{"name":"debug","inherits":"base"}],"testPresets":[{"name":"tests","configurePreset":"debug"}]})"));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("go.mod")), "module example.test/project\n"));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("go.work")), "go 1.22\n"));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("pytest.ini")), "[pytest]\naddopts=-ra\n"));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("package.json")), R"({"scripts":{"build":"tsc","verify":"eslint .","test:unit":"vitest"}})"));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("Makefile")), "all:\n\t@echo ok\n"));

        BuildSystemDetector detector;
        auto result = detector.detect(QStringLiteral("workspace"), temporary.path());
        QVERIFY(result);
        const auto project = result.value();
        QVERIFY(project.hasCMake); QVERIFY(project.hasGoModule); QVERIFY(project.hasGoWorkspace);
        QVERIFY(project.hasPytest); QVERIFY(project.hasNode); QVERIFY(project.hasMake);
        QCOMPARE(project.configurePresets, QStringList{QStringLiteral("debug")});
        QVERIFY(project.buildPresets.isEmpty());
        QCOMPARE(project.testPresets, QStringList{QStringLiteral("tests")});
        QCOMPARE(project.nodeScripts.size(), 3);
        QCOMPARE(project.presetBinaryDirectories.value(QStringLiteral("debug")),
                 QDir(temporary.path()).filePath(QStringLiteral("out/debug")));
        const auto profiles = detector.defaultProfiles(project);
        QVERIFY(std::any_of(profiles.cbegin(), profiles.cend(), [](const BuildProfile& profile) {
            return profile.runner == RunnerType::CMakeBuild && profile.preset.isEmpty()
                && profile.configuration == QStringLiteral("Release");
        }));
        QVERIFY(std::none_of(profiles.cbegin(), profiles.cend(), [](const BuildProfile& profile) {
            return profile.runner == RunnerType::CMakeBuild && profile.preset == QStringLiteral("debug");
        }));
        QVERIFY(std::any_of(profiles.cbegin(), profiles.cend(), [](const BuildProfile& profile) {
            return profile.runner == RunnerType::CTest && profile.preset == QStringLiteral("tests") && profile.testAction;
        }));
    }

    void detectsGoOnlyWorkspace()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("go.work")), "go 1.22\n"));
        BuildSystemDetector detector;
        auto result = detector.detect(QStringLiteral("workspace"), temporary.path());
        QVERIFY(result); QVERIFY(result.value().hasGoWorkspace); QVERIFY(!result.value().hasGoModule);
        const auto profiles = detector.defaultProfiles(result.value());
        QCOMPARE(profiles.size(), 2);
        QVERIFY(profiles.at(0).runner == RunnerType::GoBuild || profiles.at(1).runner == RunnerType::GoBuild);
        QVERIFY(profiles.at(0).testAction || profiles.at(1).testAction);
    }

    void resolvesPresetBuildTreesAndWithholdsUnknownMacroPaths()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("CMakeLists.txt")), "project(test)\n"));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("CMakePresets.json")),
            R"({"version":6,"configurePresets":[{"name":"base","hidden":true,"generator":"Ninja Multi-Config","binaryDir":"${sourceDir}/out/${presetName}"},{"name":"debug","inherits":"base"}],"buildPresets":[{"name":"build-base","hidden":true,"configurePreset":"debug"},{"name":"release","inherits":"build-base","configuration":"Release"}],"testPresets":[{"name":"test-base","hidden":true,"configurePreset":"debug"},{"name":"tests","inherits":"test-base"}]})"));
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("CMakeUserPresets.json")),
            R"({"version":6,"configurePresets":[{"name":"external-output","generator":"Ninja","binaryDir":"$env{OUTSIDE_ROOT}/build"}]})"));
        BuildSystemDetector detector;
        auto result = detector.detect(QStringLiteral("workspace"), temporary.path());
        QVERIFY(result);
        const auto project = result.value();
        QCOMPARE(project.presetBinaryDirectories.value(QStringLiteral("debug")),
            QDir(temporary.path()).filePath(QStringLiteral("out/debug")));
        QVERIFY(!project.presetBinaryDirectories.contains(QStringLiteral("external-output")));
        QCOMPARE(project.buildPresetBinaryDirectories.value(QStringLiteral("release")),
            QDir(temporary.path()).filePath(QStringLiteral("out/debug")));
        QCOMPARE(project.testPresetBinaryDirectories.value(QStringLiteral("tests")),
            QDir(temporary.path()).filePath(QStringLiteral("out/debug")));
        const auto profiles = detector.defaultProfiles(project);
        QVERIFY(std::any_of(profiles.cbegin(), profiles.cend(), [&](const BuildProfile& profile) {
            return profile.runner == RunnerType::CMakeBuild && profile.preset == QStringLiteral("release")
                && profile.buildDirectory == QDir(temporary.path()).filePath(QStringLiteral("out/debug"));
        }));
        QVERIFY(std::none_of(profiles.cbegin(), profiles.cend(), [](const BuildProfile& profile) {
            return profile.name.contains(QStringLiteral("external-output"));
        }));
    }

    void parsesGoJUnitAndCompilerResults()
    {
        const QByteArray go = "{\"Action\":\"run\",\"Package\":\"example/p\",\"Test\":\"TestPass\"}\n"
            "{\"Action\":\"output\",\"Package\":\"example/p\",\"Test\":\"TestPass\",\"Output\":\"ok\\n\"}\n"
            "{\"Action\":\"pass\",\"Package\":\"example/p\",\"Test\":\"TestPass\",\"Elapsed\":0.01}\n"
            "{\"Action\":\"fail\",\"Package\":\"example/p\",\"Test\":\"TestBad\",\"Elapsed\":0.2}\n";
        const auto goTests = BuildResultParser::parseGoJson(go);
        QCOMPARE(goTests.size(), 2);
        QCOMPARE(goTests.at(0).status, QStringLiteral("passed"));
        QCOMPARE(goTests.at(1).status, QStringLiteral("failed"));
        QCOMPARE(goTests.at(1).packageName, QStringLiteral("example/p"));

        const auto junit = BuildResultParser::parseJUnit(QByteArrayLiteral(
            "<testsuite tests='3'><testcase classname='pkg' name='one' time='0.05'/><testcase classname='pkg' name='two'><failure message='bad'>trace</failure></testcase><testcase classname='pkg' name='three'><skipped message='skip'/></testcase></testsuite>"));
        QCOMPARE(junit.size(), 3);
        QCOMPARE(junit.at(1).status, QStringLiteral("failed"));
        QVERIFY(junit.at(1).details.contains(QStringLiteral("trace")));
        QCOMPARE(junit.at(2).status, QStringLiteral("skipped"));

        const auto diagnostics = BuildResultParser::parseDiagnostics(
            QStringLiteral("src/main.cpp:12:8: error: missing symbol\nfile.cpp(9,3): warning C4996: old api"), QStringLiteral("Build"));
        QCOMPARE(diagnostics.size(), 2);
        QCOMPARE(diagnostics.at(0).line, 12);
        QCOMPARE(diagnostics.at(1).column, 3);
        QCOMPARE(diagnostics.at(1).severity, QStringLiteral("warning"));
    }

    void runsSuccessfulAndCancelledJobsWithBoundedLogs()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        application::workspaces::WorkspaceContext workspaceContext;
        const QString workspaceId = QStringLiteral("b1111111-1111-4111-8111-111111111111");
        const QString accountId = QStringLiteral("a1111111-1111-4111-8111-111111111111");
        workspaceContext.activate(makeWorkspace(temporary.path(), workspaceId, accountId));
        MemoryHistory history;
        FakeLauncher launcher;
        shared::security::RedactionService redactor;
        BuildExecutionCoordinator coordinator(history, launcher, workspaceContext,
            temporary.filePath(QStringLiteral("logs")), redactor);
        BuildProfile profile;
        profile.id = QStringLiteral("profile"); profile.workspaceId = workspaceId;
        profile.name = QStringLiteral("Fake command"); profile.runner = RunnerType::Custom;
        profile.program = QCoreApplication::applicationFilePath(); profile.workingDirectory = temporary.path();
        profile.trusted = true; profile.timeoutSeconds = 10;

        auto resolved = coordinator.resolve(profile, temporary.path());
        QVERIFY(resolved); QCOMPARE(resolved.value().executable, QFileInfo(profile.program).canonicalFilePath());
        QVERIFY(!coordinator.resolve(profile, QDir(temporary.path()).filePath(QStringLiteral("missing"))));

        QSignalSpy finished(&coordinator, &BuildExecutionCoordinator::runFinished);
        auto started = coordinator.start(profile, temporary.path());
        QVERIFY(started); QVERIFY(coordinator.running());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        const auto success = qvariant_cast<BuildRun>(finished.takeFirst().at(0));
        QCOMPARE(success.status, RunStatus::Succeeded);
        QCOMPARE(success.exitCode, 0);
        QVERIFY(QFileInfo::exists(success.logReference));
        QCOMPARE(QFileInfo(success.logReference).size(), launcher.output.size());

        launcher.mode = FakeProcess::FinishMode::Wait;
        launcher.output = QByteArray(5 * 1024 * 1024, 'x');
        started = coordinator.start(profile, temporary.path());
        QVERIFY(started);
        QTest::qWait(40);
        coordinator.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        const auto cancelled = qvariant_cast<BuildRun>(finished.takeFirst().at(0));
        QCOMPARE(cancelled.status, RunStatus::Cancelled);
        QVERIFY(QFileInfo(cancelled.logReference).size() <= 4 * 1024 * 1024);

        launcher.mode = FakeProcess::FinishMode::Success;
        started = coordinator.start(profile, temporary.path());
        QVERIFY(started);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        const auto truncated = qvariant_cast<BuildRun>(finished.takeFirst().at(0));
        QCOMPARE(truncated.status, RunStatus::Succeeded);
        QVERIFY(truncated.summary.contains(QStringLiteral("truncated")));
        QVERIFY(QFileInfo(truncated.logReference).size() <= 4 * 1024 * 1024);

        launcher.mode = FakeProcess::FinishMode::Wait;
        profile.timeoutSeconds = 1;
        started = coordinator.start(profile, temporary.path());
        QVERIFY(started);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        const auto timedOut = qvariant_cast<BuildRun>(finished.takeFirst().at(0));
        QCOMPARE(timedOut.status, RunStatus::TimedOut);
        QVERIFY(timedOut.status != RunStatus::Succeeded);

        workspaceContext.activate(makeWorkspace(temporary.path(), workspaceId, accountId));
        profile.timeoutSeconds = 10;
        started = coordinator.start(profile, temporary.path());
        QVERIFY(started);
        const QString otherWorkspaceId = QStringLiteral("c1111111-1111-4111-8111-111111111111");
        workspaceContext.activate(makeWorkspace(temporary.path(), otherWorkspaceId, accountId));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        const auto switched = qvariant_cast<BuildRun>(finished.takeFirst().at(0));
        QCOMPARE(switched.status, RunStatus::Cancelled);
    }

    void persistsWorkspaceScopedProfilesAndResults()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        infrastructure::database::InternalDatabaseManager database(
            temporary.filePath(QStringLiteral("state.db")), temporary.filePath(QStringLiteral("backups")));
        QVERIFY(database.open()); QVERIFY(database.migrate());
        QSqlQuery setup(database.database());
        QVERIFY(setup.exec(QStringLiteral("INSERT INTO accounts(id,username_normalized,display_username,password_hash) VALUES('a1111111-1111-4111-8111-111111111111','user','User','hash')")));
        QVERIFY(setup.exec(QStringLiteral("INSERT INTO workspaces(id,account_id,display_name,root_path,entered_path,canonical_root_path) VALUES('b1111111-1111-4111-8111-111111111111','a1111111-1111-4111-8111-111111111111','Project','C:/Project','C:/Project','C:/Project')")));
        auto account = domain::account::AccountId::fromString(QStringLiteral("a1111111-1111-4111-8111-111111111111"));
        QVERIFY(account);
        infrastructure::buildtest::SqlBuildHistoryRepository repository(database, account.value());
        BuildProfile profile; profile.id = QStringLiteral("profile-a"); profile.workspaceId = QStringLiteral("b1111111-1111-4111-8111-111111111111");
        profile.name = QStringLiteral("Unit tests"); profile.runner = RunnerType::GoTest; profile.testAction = true;
        profile.arguments = {QStringLiteral("./...")};
        QVERIFY(repository.saveProfile(profile));
        QVERIFY(repository.setProfileTrusted(profile.workspaceId, profile.id, true));
        auto profiles = repository.profiles(profile.workspaceId);
        QVERIFY(profiles); QCOMPARE(profiles.value().size(), 1); QVERIFY(profiles.value().front().trusted);

        BuildRun run; run.id = QStringLiteral("run-a"); run.workspaceId = profile.workspaceId; run.profileId = profile.id;
        run.profileName = profile.name; run.runner = profile.runner; run.testAction = true;
        run.startedAtUtc = QDateTime::currentDateTimeUtc(); run.finishedAtUtc = run.startedAtUtc.addSecs(1);
        run.status = RunStatus::Failed; run.exitCode = 1; run.durationMilliseconds = 1000; run.summary = QStringLiteral("1 failed");
        run.tests.append({QStringLiteral("test-a"), run.id, QStringLiteral("example/pkg"), QStringLiteral("TestFails"),
                          QStringLiteral("failed"), 45, QStringLiteral("assertion detail")});
        run.totalTests=1; run.failedTests=1;
        QVERIFY(repository.startRun(run));
        const auto savedRun = repository.finishRun(run);
        QVERIFY2(savedRun, qPrintable(savedRun.error().technicalMessage));
        auto runs = repository.recentRuns(profile.workspaceId);
        QVERIFY(runs); QCOMPARE(runs.value().size(), 1); QCOMPARE(runs.value().front().status, RunStatus::Failed);
        auto tests = repository.testResults(profile.workspaceId, run.id);
        QVERIFY(tests); QCOMPARE(tests.value().size(), 1); QCOMPARE(tests.value().front().name, QStringLiteral("TestFails"));

        auto otherAccount = domain::account::AccountId::fromString(QStringLiteral("a2222222-2222-4222-8222-222222222222"));
        QVERIFY(otherAccount);
        infrastructure::buildtest::SqlBuildHistoryRepository isolated(database, otherAccount.value());
        QVERIFY(isolated.profiles(profile.workspaceId).value().isEmpty());
        QVERIFY(database.close());
    }
};

QTEST_GUILESS_MAIN(BuildTestTest)
#include "buildtest_test.moc"
