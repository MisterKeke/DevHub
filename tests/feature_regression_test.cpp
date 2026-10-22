#include "application/services/ServiceCoordinator.h"
#include "application/git/GitService.h"
#include "application/services/ManagedProcess.h"
#include "application/settings/SettingsService.h"
#include "application/support/SupportBundleBuilder.h"
#include "infrastructure/database/NativeSqliteQueryExecutor.h"
#include "infrastructure/database/QtSqlDatabaseSession.h"
#include "infrastructure/database/QtSqlDatabaseWorker.h"
#include "infrastructure/database/QtSqlConnectionFactory.h"
#include "presentation/databases/DatabasePage.h"

#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QProcess>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <QtEndian>
#include <thread>
#include <QtTest/QtTest>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace devhub;
using namespace application::databases;
using namespace application::services;
template <class T> using Result = shared::common::Result<T>;

namespace
{
class ServiceRepository final : public IServiceDefinitionRepository
{
  public:
    Result<QVector<domain::services::ServiceDefinition>> list(const QString &) override
    {
        return Result<QVector<domain::services::ServiceDefinition>>::success({});
    }
    Result<void> save(const domain::services::ServiceDefinition &) override
    {
        return Result<void>::success();
    }
    Result<void> remove(const QString &, const QString &) override
    {
        return Result<void>::success();
    }
};

class FakeProcess final : public IManagedProcess
{
  public:
    FakeProcess(bool autoStart, QObject *parent) : IManagedProcess(parent), m_autoStart(autoStart)
    {
    }
    void start() override
    {
        if (m_autoStart)
            QTimer::singleShot(0, this, [this] { emit started(); });
    }
    void stop(int) override
    {
        QTimer::singleShot(0, this, [this] { emit finished(0, 0); });
    }
    qint64 processId() const override
    {
        return m_autoStart ? 123 : 0;
    }

  private:
    bool m_autoStart;
};

class FakeLauncher final : public IProcessLauncher
{
  public:
    bool autoStart = true;
    int launches = 0;
    IManagedProcess *launch(const domain::services::ServiceDefinition &, QObject *parent) override
    {
        ++launches;
        auto *process = new FakeProcess(autoStart, parent);
        return process;
    }
};

domain::services::ServiceDefinition definition(const QString &id, const QString &root)
{
    domain::services::ServiceDefinition value;
    value.id = id;
    value.name = id;
    value.program = QStringLiteral("devhub-regression-missing-executable");
    value.workingDirectory = root;
    return value;
}

class References final : public IDatabaseReferenceRepository
{
  public:
    QString filePath = QStringLiteral("unused.sqlite");
    Result<QVector<DatabaseReference>> list(const QString &workspace) override
    {
        return Result<QVector<DatabaseReference>>::success(
            {{QStringLiteral("db"), workspace, QStringLiteral("Test"),
              filePath}});
    }
    Result<void> add(const DatabaseReference &) override
    {
        return Result<void>::success();
    }
    Result<void> remove(const QString &, const QString &) override
    {
        return Result<void>::success();
    }
};

class FakeSession final : public IDatabaseSession
{
  public:
    using IDatabaseSession::IDatabaseSession;
    int pageRequests = 0;
    bool wasClosed = false;
    void open(QString, DatabaseOpenMode) override
    {
        QTimer::singleShot(0, this,
                           [this] { emit openFinished(true, {}, DatabaseOpenMode::ReadWrite); });
    }
    void requestSchema() override
    {
        emit schemaReady({{DatabaseObjectKind::Table, QStringLiteral("things"), {}, {}}});
    }
    void requestColumns(QString name) override
    {
        QVector<DatabaseColumn> columns(2);
        columns[0].name = QStringLiteral("id");
        columns[0].primaryKeyOrder = 1;
        columns[1].name = QStringLiteral("value");
        columns[1].declaredType = QStringLiteral("TEXT");
        emit columnsReady(name, columns, false);
    }
    void requestPage(QString name, int offset, int size, QString, bool) override
    {
        ++pageRequests;
        QTimer::singleShot(0, this,
                           [this, name, offset, size]
                           {
                               DatabasePageResult page;
                               page.columns = {QStringLiteral("id"), QStringLiteral("value")};
                               page.primaryKeyColumns = {0};
                               page.offset = offset;
                               page.pageSize = size;
                               page.hasMore = true;
                               for (int i = 0; i < size; ++i)
                                   page.rows.append({offset + i, QStringLiteral("original")});
                               emit pageReady(name, page);
                           });
    }
    void requestCountRows(QString) override {}
    void requestQuery(QString, bool) override {}
    void requestBackup(QString) override {}
    void requestCommit(PendingChanges) override {}
    void cancelQuery() override {}
    void close() override
    {
        wasClosed = true;
    }
};

class FakeFactory final : public ISqliteConnectionFactory
{
  public:
    FakeSession *session = nullptr;
    IDatabaseSession *createSession(QObject *parent) override
    {
        session = new FakeSession(parent);
        return session;
    }
};

QPushButton *button(QWidget &page, const QString &text)
{
    for (auto *value : page.findChildren<QPushButton *>())
        if (value->text() == text)
            return value;
    return nullptr;
}

void answerDialog(QMessageBox::StandardButton answer)
{
    QTimer::singleShot(0,
                       [answer]
                       {
                           for (auto *widget : QApplication::topLevelWidgets())
                           {
                               if (auto *box = qobject_cast<QMessageBox *>(widget))
                                   box->button(answer)->click();
                           }
                       });
}

class SettingsRepository final : public application::settings::ISettingsRepository
{
  public:
    Result<application::settings::SettingValues> load(application::settings::SettingScope,
                                                      const QString &) override
    {
        return Result<application::settings::SettingValues>::success({});
    }
    Result<void> save(application::settings::SettingScope, const QString &, const QString &,
                      const QVariant &) override
    {
        return Result<void>::success();
    }
};
} // namespace

class FeatureRegressionTest final : public QObject
{
    Q_OBJECT
  private slots:
    void failedLaunchCanRetryAndStop()
    {
        QTemporaryDir root;
        ServiceRepository repository;
        QProcessLauncher launcher;
        ServiceCoordinator coordinator(repository, launcher);
        const auto service = definition(QStringLiteral("missing"), root.path());
        QVERIFY(coordinator.start(service, root.path()));
        QTRY_COMPARE(coordinator.states().first().lifecycle, ServiceLifecycleState::Failed);
        QVERIFY(coordinator.start(service, root.path()));
        QTRY_COMPARE(coordinator.states().first().lifecycle, ServiceLifecycleState::Failed);
        QVERIFY(coordinator.stop(service.id));
        QVERIFY(!coordinator.hasRunningServices());
    }

    void startAllStartsEveryLongRunningService()
    {
        QTemporaryDir root;
        ServiceRepository repository;
        FakeLauncher launcher;
        ServiceCoordinator coordinator(repository, launcher);
        QVector<domain::services::ServiceDefinition> services;
        for (int i = 0; i < 5; ++i)
            services.append(definition(QString::number(i), root.path()));
        coordinator.startAll(services, root.path(), 3);
        QCOMPARE(launcher.launches, 3);
        QTRY_COMPARE(launcher.launches, 5);
        coordinator.stopAll();
        QTRY_VERIFY(!coordinator.hasRunningServices());
    }

    void stoppingBeforeLaunchFailsDoesNotHang()
    {
        QTemporaryDir root;
        ServiceRepository repository;
        QProcessLauncher launcher;
        ServiceCoordinator coordinator(repository, launcher);
        const auto service = definition(QStringLiteral("missing"), root.path());
        QVERIFY(coordinator.start(service, root.path()));
        QVERIFY(coordinator.stop(service.id));
        QTRY_VERIFY(!coordinator.hasRunningServices());
        QVERIFY(coordinator.start(service, root.path()));
        QTRY_COMPARE(coordinator.states().first().lifecycle, ServiceLifecycleState::Failed);
    }

    void stopAllCancelsQueuedStarts()
    {
        QTemporaryDir root;
        ServiceRepository repository;
        FakeLauncher launcher;
        launcher.autoStart = false;
        ServiceCoordinator coordinator(repository, launcher);
        QVector<domain::services::ServiceDefinition> services;
        for (int i = 0; i < 5; ++i)
            services.append(definition(QString::number(i), root.path()));
        coordinator.startAll(services, root.path(), 3);
        coordinator.stopAll();
        QTest::qWait(250);
        QCOMPARE(launcher.launches, 3);
        QVERIFY(!coordinator.hasRunningServices());
    }

    void stopCancelsScheduledRestart()
    {
        QTemporaryDir root;
        ServiceRepository repository;
        QProcessLauncher launcher;
        ServiceCoordinator coordinator(repository, launcher);
        auto service = definition(QStringLiteral("missing"), root.path());
        service.restartPolicy = domain::services::RestartPolicy::Always;
        QVERIFY(coordinator.start(service, root.path()));
        QTRY_COMPARE(coordinator.states().first().lifecycle, ServiceLifecycleState::Failed);
        coordinator.stopAll();
        QSignalSpy states(&coordinator, &ServiceCoordinator::stateChanged);
        QTest::qWait(1200);
        QCOMPARE(states.size(), 0);
    }

    void gitTypedMutationsRejectOptionInputsInDisposableRepository()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString git = QStandardPaths::findExecutable(QStringLiteral("git")); QVERIFY(!git.isEmpty());
        const QString repository = root.filePath(QStringLiteral("repo")); QVERIFY(QDir().mkpath(repository));
        auto run = [&](QStringList arguments) {
            QProcess process; process.setProgram(git); process.setArguments(arguments); process.setWorkingDirectory(repository);
            process.start(); if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) return -1;
            return process.exitCode();
        };
        QCOMPARE(run({QStringLiteral("init")}), 0);
        QCOMPARE(run({QStringLiteral("symbolic-ref"), QStringLiteral("HEAD"), QStringLiteral("refs/heads/main")}), 0);
        QFile seed(root.filePath(QStringLiteral("repo/seed.txt")));
        QVERIFY(seed.open(QIODevice::WriteOnly)); seed.write("fixture\n"); seed.close();
        QCOMPARE(run({QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("seed.txt")}), 0);
        QCOMPARE(run({QStringLiteral("-c"), QStringLiteral("user.name=Regression"), QStringLiteral("-c"),
                      QStringLiteral("user.email=regression@example.invalid"), QStringLiteral("commit"),
                      QStringLiteral("-m"), QStringLiteral("fixture")}), 0);
        QCOMPARE(run({QStringLiteral("branch"), QStringLiteral("victim")}), 0);
        application::git::GitService service{application::git::GitCommandRunner(git)};
        QVERIFY(!service.createBranch(repository, QStringLiteral("-D"), QStringLiteral("victim")));
        QVERIFY(!service.checkout(repository, QStringLiteral("-D")));
        QVERIFY(!service.renameBranch(repository, QStringLiteral("victim"), QStringLiteral("-D")));
        QVERIFY(!service.deleteBranch(repository, QStringLiteral("-D")));
        QVERIFY(!service.fetch(repository, QStringLiteral("-D")));
        QVERIFY(!service.pull(repository, QStringLiteral("-D"), QStringLiteral("victim"), QStringLiteral("merge")));
        QVERIFY(!service.preparePush(repository, QStringLiteral("-D"), QStringLiteral("victim"), false));
        QVERIFY(!service.merge(repository, QStringLiteral("-D")));
        QVERIFY(!service.stashApply(repository, QStringLiteral("-D"), false));
        QCOMPARE(run({QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("refs/heads/victim")}), 0);
        auto created = service.createBranch(repository, QStringLiteral("safe-name"), QStringLiteral("HEAD"));
        QVERIFY2(created, qPrintable(created ? QString{} : created.error().userMessage));
        QVERIFY(service.renameBranch(repository, QStringLiteral("safe-name"), QStringLiteral("renamed")));
        QVERIFY(service.checkout(repository, QStringLiteral("renamed")));
        QVERIFY(service.deleteBranch(repository, QStringLiteral("victim")));
    }

    void gitPushPreviewPinsFirstPublicationAndRejectsExpandedOrStalePlans()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString git = QStandardPaths::findExecutable(QStringLiteral("git")); QVERIFY(!git.isEmpty());
        const QString repository = root.filePath(QStringLiteral("repo"));
        const QString bare = root.filePath(QStringLiteral("remote.git"));
        const QString alternateBare = root.filePath(QStringLiteral("alternate.git"));
        QVERIFY(QDir().mkpath(repository));
        QVERIFY(QDir().mkpath(alternateBare));
        const auto run = [git](const QString& directory, QStringList arguments) {
            QProcess process; process.setProgram(git); process.setArguments(std::move(arguments));
            process.setWorkingDirectory(directory); process.start();
            return process.waitForStarted(3000) && process.waitForFinished(10000) && process.exitCode() == 0;
        };
        QVERIFY(run(repository, {QStringLiteral("init"), QStringLiteral("--quiet"),
            QStringLiteral("--initial-branch=feature/local")}));
        QVERIFY(run(bare, {QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet")}));
        QVERIFY(run(alternateBare, {QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet")}));
        QFile seed(QDir(repository).filePath(QStringLiteral("seed.txt")));
        QVERIFY(seed.open(QIODevice::WriteOnly)); seed.write("fixture\n"); seed.close();
        QVERIFY(run(repository, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("seed.txt")}));
        QVERIFY(run(repository, {QStringLiteral("-c"), QStringLiteral("user.name=Regression"),
            QStringLiteral("-c"), QStringLiteral("user.email=regression@example.invalid"),
            QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("first commit"), QStringLiteral("--quiet")}));
        application::git::GitService service{application::git::GitCommandRunner(git)};
        QVERIFY(!service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("published/server"), true));
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), bare}));

        QFile dirty(QDir(repository).filePath(QStringLiteral("seed.txt")));
        QVERIFY(dirty.open(QIODevice::Append)); dirty.write("uncommitted\n"); dirty.close();
        auto first = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("published/server"), true);
        QVERIFY2(first, qPrintable(first ? QString{} : first.error().userMessage));
        QCOMPARE(first.value().localBranch, QStringLiteral("feature/local"));
        QCOMPARE(first.value().targetBranch, QStringLiteral("published/server"));
        QVERIFY(first.value().sourceCommit.size() == 40 || first.value().sourceCommit.size() == 64);
        QVERIFY(first.value().upstreamChange);
        QCOMPARE(first.value().remoteState, QStringLiteral("notQueried"));
        QVERIFY(!first.value().outgoingCountAvailable);
        auto published = service.executePush(repository, first.value());
        QVERIFY2(published, qPrintable(published ? QString{} : published.error().userMessage));
        QVERIFY(published.value().outcome == application::git::GitPushOutcome::Published);
        QVERIFY(published.value().remotePublished);
        QVERIFY(published.value().trackingConfigured);
        QVERIFY(run(bare, {QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
            QStringLiteral("refs/heads/published/server")}));
        auto status = service.status(repository);
        QVERIFY(status);
        QVERIFY(!status.value().files.isEmpty()); // Dirty files remain outside the committed push.
        auto existingUpstream = service.preparePush(repository);
        QVERIFY(existingUpstream);
        QCOMPARE(existingUpstream.value().upstream, QStringLiteral("origin/published/server"));

        QFile second(QDir(repository).filePath(QStringLiteral("second.txt")));
        QVERIFY(second.open(QIODevice::WriteOnly)); second.write("second\n"); second.close();
        QVERIFY(run(repository, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("second.txt")}));
        QVERIFY(run(repository, {QStringLiteral("-c"), QStringLiteral("user.name=Regression"),
            QStringLiteral("-c"), QStringLiteral("user.email=regression@example.invalid"),
            QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("second commit"), QStringLiteral("--quiet")}));
        auto staleSource = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false);
        QVERIFY(staleSource);
        QFile third(QDir(repository).filePath(QStringLiteral("third.txt")));
        QVERIFY(third.open(QIODevice::WriteOnly)); third.write("third\n"); third.close();
        QVERIFY(run(repository, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("third.txt")}));
        QVERIFY(run(repository, {QStringLiteral("-c"), QStringLiteral("user.name=Regression"),
            QStringLiteral("-c"), QStringLiteral("user.email=regression@example.invalid"),
            QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("third commit"), QStringLiteral("--quiet")}));
        auto rejectedStaleSource = service.executePush(repository, staleSource.value());
        QVERIFY(rejectedStaleSource);
        QVERIFY(rejectedStaleSource.value().outcome == application::git::GitPushOutcome::PreviewChanged);

        const QString hookPath = QDir(repository).filePath(QStringLiteral(".git/hooks/pre-push"));
        auto writeHook = [&hookPath](const QByteArray& contents) {
            QFile hook(hookPath);
            if (!hook.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            hook.write(contents); hook.close();
            return hook.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        };
        QVERIFY(writeHook("#!/bin/sh\ngit config --local user.name hook-updated\nexit 0\n"));
        auto trackingPartial = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("tracking-failure"), true);
        QVERIFY(trackingPartial);
        auto partialResult = service.executePush(repository, trackingPartial.value());
        QVERIFY(partialResult);
        QVERIFY(partialResult.value().outcome == application::git::GitPushOutcome::PartialTrackingFailure);
        QVERIFY(partialResult.value().remotePublished);
        QVERIFY(run(bare, {QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
            QStringLiteral("refs/heads/tracking-failure")}));
        QVERIFY(writeHook("#!/bin/sh\nexit 1\n"));
        auto hookRejected = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("hook-failure"), false);
        QVERIFY(hookRejected);
        auto hookResult = service.executePush(repository, hookRejected.value());
        QVERIFY(hookResult);
        QVERIFY(hookResult.value().outcome == application::git::GitPushOutcome::HookRejected);
        QVERIFY(QFile::remove(hookPath));

        auto changedDestination = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false);
        QVERIFY(changedDestination);
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--push"),
            QStringLiteral("origin"), alternateBare}));
        auto rejectedDestination = service.executePush(repository, changedDestination.value());
        QVERIFY(rejectedDestination);
        QVERIFY(rejectedDestination.value().outcome == application::git::GitPushOutcome::PreviewChanged);

        const QString rewriteKey = QStringLiteral("url.\"%1\".insteadOf")
            .arg(QDir::fromNativeSeparators(alternateBare));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--add"),
            rewriteKey, QStringLiteral("push-alias:")}));
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--push"),
            QStringLiteral("origin"), QStringLiteral("push-alias:repo")}));
        auto rewrittenDestination = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false);
        QVERIFY(rewrittenDestination);
        QVERIFY(rewrittenDestination.value().pushUrlForExecution.contains(QStringLiteral("alternate.git")));
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--push"),
            QStringLiteral("origin"), bare}));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--unset-all"),
            rewriteKey}));

        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--push"),
            QStringLiteral("--add"), QStringLiteral("origin"), bare}));
        auto multipleDestinations = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false);
        QVERIFY(!multipleDestinations);
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--push"),
            QStringLiteral("origin"), bare}));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"),
            QStringLiteral("remote.origin.mirror"), QStringLiteral("true")}));
        QVERIFY(!service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"),
            QStringLiteral("--unset"), QStringLiteral("remote.origin.mirror")}));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"),
            QStringLiteral("push.followTags"), QStringLiteral("true")}));
        QVERIFY(!service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"),
            QStringLiteral("--unset"), QStringLiteral("push.followTags")}));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--add"),
            QStringLiteral("remote.origin.push"), QStringLiteral("refs/heads/*:refs/heads/*")}));
        QVERIFY(!service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"),
            QStringLiteral("--unset-all"), QStringLiteral("remote.origin.push")}));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"),
            QStringLiteral("push.recurseSubmodules"), QStringLiteral("on-demand")}));
        QVERIFY(!service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false));
        QVERIFY(run(repository, {QStringLiteral("config"), QStringLiteral("--local"),
            QStringLiteral("--unset"), QStringLiteral("push.recurseSubmodules")}));

        QVERIFY(run(repository, {QStringLiteral("checkout"), QStringLiteral("--detach"), QStringLiteral("HEAD")}));
        QVERIFY(!service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("other-name"), false));
        const QString unborn = root.filePath(QStringLiteral("unborn"));
        QVERIFY(QDir().mkpath(unborn));
        QVERIFY(run(unborn, {QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--initial-branch=empty" )}));
        QVERIFY(!service.preparePush(unborn, QStringLiteral("origin"), QStringLiteral("empty"), false));
    }

    void gitPushUsesApprovedDestinationBeforeCommitRefspec_data()
    {
        QTest::addColumn<bool>("useFileUrl");
        QTest::newRow("local path with spaces") << false;
        QTest::newRow("file URL with spaces") << true;
    }

    void gitPushUsesApprovedDestinationBeforeCommitRefspec()
    {
        QFETCH(bool, useFileUrl);
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString git = QStandardPaths::findExecutable(QStringLiteral("git")); QVERIFY(!git.isEmpty());
        const QString repository = root.filePath(QStringLiteral("working repository"));
        const QString fetchBare = root.filePath(QStringLiteral("fetch destination.git"));
        const QString pushBare = root.filePath(QStringLiteral("approved push destination.git"));
        QVERIFY(QDir().mkpath(repository));
        const auto run = [git](const QString& directory, QStringList arguments, QByteArray* output = nullptr) {
            QProcess process; process.setProgram(git); process.setArguments(std::move(arguments));
            process.setWorkingDirectory(directory); process.start();
            const bool success = process.waitForStarted(3000) && process.waitForFinished(10000)
                && process.exitCode() == 0;
            if (output) *output = process.readAllStandardOutput().trimmed();
            return success;
        };
        QVERIFY(run(repository, {QStringLiteral("init"), QStringLiteral("--quiet"),
            QStringLiteral("--initial-branch=feature/local")}));
        QVERIFY(run(root.path(), {QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet"), fetchBare}));
        QVERIFY(run(root.path(), {QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet"), pushBare}));
        QVERIFY(run(repository, {QStringLiteral("-c"), QStringLiteral("user.name=Regression"),
            QStringLiteral("-c"), QStringLiteral("user.email=regression@example.invalid"),
            QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"),
            QStringLiteral("approved publication"), QStringLiteral("--quiet")}));
        // A distinct fetch URL catches fallback to the remote's fetch destination.
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("reviewed"), fetchBare}));
        const QString pushUrl = useFileUrl ? QUrl::fromLocalFile(pushBare).toString(QUrl::FullyEncoded) : pushBare;
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("--push"),
            QStringLiteral("reviewed"), pushUrl}));

        application::git::GitService service{application::git::GitCommandRunner(git)};
        auto preview = service.preparePush(repository, QStringLiteral("reviewed"), QStringLiteral("published/server"), true);
        QVERIFY2(preview, qPrintable(preview ? QString{} : preview.error().userMessage));
        QCOMPARE(preview.value().pushUrlForExecution, pushUrl);
        auto published = service.executePush(repository, preview.value());
        QVERIFY2(published, qPrintable(published ? QString{} : published.error().userMessage));
        QVERIFY2(published.value().outcome == application::git::GitPushOutcome::Published,
            qPrintable(published.value().message + QLatin1Char('\n') + published.value().diagnostic));
        QVERIFY(published.value().remotePublished);
        QVERIFY(published.value().trackingConfigured);
        QByteArray remoteCommit;
        QVERIFY(run(pushBare, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
            QStringLiteral("refs/heads/published/server")}, &remoteCommit));
        QCOMPARE(QString::fromLatin1(remoteCommit), preview.value().sourceCommit);
        QVERIFY(!run(fetchBare, {QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
            QStringLiteral("refs/heads/published/server")}));

        auto existingUpstream = service.preparePush(repository);
        QVERIFY(existingUpstream);
        QCOMPARE(existingUpstream.value().remote, QStringLiteral("reviewed"));
        QCOMPARE(existingUpstream.value().targetBranch, QStringLiteral("published/server"));
        auto repeated = service.executePush(repository, existingUpstream.value());
        QVERIFY(repeated);
        QVERIFY2(repeated.value().outcome == application::git::GitPushOutcome::AlreadyUpToDate,
            qPrintable(repeated.value().message + QLatin1Char('\n') + repeated.value().diagnostic));
    }

    void gitPushPorcelainResultsClassifyFailuresAndRedactCredentials()
    {
        application::git::GitPushPreview preview;
        preview.sourceCommit = QString(40, QLatin1Char('a'));
        preview.remote = QStringLiteral("origin");
        preview.targetBranch = QStringLiteral("release/name");
        auto classify = [&preview](QByteArray standardOutput, QByteArray standardError = {}, int exitCode = 1,
                                   bool timedOut = false, bool cancelled = false, bool processStarted = true) {
            application::git::GitCommandResult process;
            process.standardOutput = std::move(standardOutput);
            process.standardError = std::move(standardError);
            process.exitCode = exitCode;
            process.timedOut = timedOut;
            process.cancelled = cancelled;
            process.processStarted = processStarted;
            return application::git::GitService::classifyPushResult(preview, process);
        };
        QVERIFY(classify({}, "fatal: Authentication failed for 'https://user:super-secret-token@example.invalid/repo'\n")
            .outcome == application::git::GitPushOutcome::AuthenticationRequired);
        const auto authentication = classify({}, "fatal: Authentication failed for 'https://user:super-secret-token@example.invalid/repo'\n");
        QVERIFY(!authentication.diagnostic.contains(QStringLiteral("super-secret-token")));
        QVERIFY(classify("! refs/heads/topic:refs/heads/release/name [rejected] (non-fast-forward)\n")
            .outcome == application::git::GitPushOutcome::NonFastForward);
        QVERIFY(classify("! refs/heads/topic:refs/heads/release/name [remote rejected] (pre-receive hook declined)\n")
            .outcome == application::git::GitPushOutcome::HookRejected);
        QVERIFY(classify("! refs/heads/topic:refs/heads/release/name [remote rejected] (protected branch)\n")
            .outcome == application::git::GitPushOutcome::RemoteRejected);
        QVERIFY(classify("= refs/heads/topic:refs/heads/release/name [up to date]\n", {}, 0)
            .outcome == application::git::GitPushOutcome::AlreadyUpToDate);
        const auto timedOut = classify({}, {}, -1, true, false, true);
        QVERIFY(timedOut.outcome == application::git::GitPushOutcome::TimedOut);
        QVERIFY(timedOut.completionUncertain);
        const auto cancelledAfterStart = classify({}, {}, -1, false, true, true);
        QVERIFY(cancelledAfterStart.outcome == application::git::GitPushOutcome::Cancelled);
        QVERIFY(cancelledAfterStart.completionUncertain);
        const auto cancelledBeforeStart = classify({}, {}, -1, false, true, false);
        QVERIFY(cancelledBeforeStart.outcome == application::git::GitPushOutcome::Cancelled);
        QVERIFY(!cancelledBeforeStart.completionUncertain);
    }

    void gitPushCancellationAfterRemoteUpdateIsReconciled()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString git = QStandardPaths::findExecutable(QStringLiteral("git")); QVERIFY(!git.isEmpty());
        const QString repository = root.filePath(QStringLiteral("repo"));
        const QString bare = root.filePath(QStringLiteral("remote.git"));
        QVERIFY(QDir().mkpath(repository));
        QVERIFY(QDir().mkpath(bare));
        const auto run = [git](const QString& directory, QStringList arguments) {
            QProcess process; process.setProgram(git); process.setArguments(std::move(arguments));
            process.setWorkingDirectory(directory); process.start();
            return process.waitForStarted(3000) && process.waitForFinished(10000) && process.exitCode() == 0;
        };
        QVERIFY(run(repository, {QStringLiteral("init"), QStringLiteral("--quiet"),
            QStringLiteral("--initial-branch=feature/cancel-test")}));
        QVERIFY(run(bare, {QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet")}));
        QFile seed(QDir(repository).filePath(QStringLiteral("seed.txt")));
        QVERIFY(seed.open(QIODevice::WriteOnly)); seed.write("seed\n"); seed.close();
        QVERIFY(run(repository, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("seed.txt")}));
        QVERIFY(run(repository, {QStringLiteral("-c"), QStringLiteral("user.name=Regression"),
            QStringLiteral("-c"), QStringLiteral("user.email=regression@example.invalid"),
            QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("cancel test commit"), QStringLiteral("--quiet")}));
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), bare}));

        const QString hookPath = QDir(bare).filePath(QStringLiteral("hooks/post-receive"));
        QFile hook(hookPath);
        QVERIFY(hook.open(QIODevice::WriteOnly | QIODevice::Truncate));
        hook.write("#!/bin/sh\ntouch \"$(dirname \"$0\")/received\"\nsleep 3\n");
        hook.close();
        QVERIFY(hook.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        application::git::GitService service{application::git::GitCommandRunner(git)};
        auto preview = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("cancelled/publication"), false);
        QVERIFY2(preview, qPrintable(preview ? QString{} : preview.error().userMessage));
        const QString received = QDir(bare).filePath(QStringLiteral("hooks/received"));
        application::tasks::CancellationSource cancellation;
        std::thread cancelAfterReceive([&] {
            QElapsedTimer timer; timer.start();
            while (!QFileInfo::exists(received) && timer.elapsed() < 10000)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (QFileInfo::exists(received)) cancellation.requestCancellation();
        });
        auto result = service.executePush(repository, preview.value(), cancellation.token());
        cancelAfterReceive.join();
        QVERIFY(cancellation.isCancellationRequested());
        QVERIFY2(result, qPrintable(result ? QString{} : result.error().userMessage));
        QCOMPARE(result.value().outcome, application::git::GitPushOutcome::Published);
        QVERIFY(result.value().remotePublished);
        QVERIFY(!result.value().completionUncertain);
        QVERIFY(run(bare, {QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
            QStringLiteral("refs/heads/cancelled/publication")}));
    }

    void gitPushSerializesAgainstOtherRepositoryMutations()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString git = QStandardPaths::findExecutable(QStringLiteral("git")); QVERIFY(!git.isEmpty());
        const QString repository = root.filePath(QStringLiteral("repo"));
        const QString bare = root.filePath(QStringLiteral("remote.git"));
        QVERIFY(QDir().mkpath(repository));
        QVERIFY(QDir().mkpath(bare));
        const auto run = [git](const QString& directory, QStringList arguments) {
            QProcess process; process.setProgram(git); process.setArguments(std::move(arguments));
            process.setWorkingDirectory(directory); process.start();
            return process.waitForStarted(3000) && process.waitForFinished(10000) && process.exitCode() == 0;
        };
        QVERIFY(run(repository, {QStringLiteral("init"), QStringLiteral("--quiet"),
            QStringLiteral("--initial-branch=feature/serialized")}));
        QVERIFY(run(bare, {QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--quiet")}));
        QFile seed(QDir(repository).filePath(QStringLiteral("seed.txt")));
        QVERIFY(seed.open(QIODevice::WriteOnly)); seed.write("seed\n"); seed.close();
        QVERIFY(run(repository, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("seed.txt")}));
        QVERIFY(run(repository, {QStringLiteral("-c"), QStringLiteral("user.name=Regression"),
            QStringLiteral("-c"), QStringLiteral("user.email=regression@example.invalid"),
            QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("serialized mutation commit"), QStringLiteral("--quiet")}));
        QVERIFY(run(repository, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), bare}));
        QFile pending(QDir(repository).filePath(QStringLiteral("pending.txt")));
        QVERIFY(pending.open(QIODevice::WriteOnly)); pending.write("pending\n"); pending.close();

        const QString hooks = QDir(repository).filePath(QStringLiteral(".git/hooks"));
        const QString hookPath = QDir(hooks).filePath(QStringLiteral("pre-push"));
        QFile hook(hookPath);
        QVERIFY(hook.open(QIODevice::WriteOnly | QIODevice::Truncate));
        hook.write("#!/bin/sh\nhook_dir=$(dirname \"$0\")\ntouch \"$hook_dir/push-started\"\ni=0\nwhile [ ! -f \"$hook_dir/release-push\" ] && [ $i -lt 300 ]; do sleep 0.02; i=$((i+1)); done\n");
        hook.close();
        QVERIFY(hook.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        application::git::GitService service{application::git::GitCommandRunner(git)};
        auto preview = service.preparePush(repository, QStringLiteral("origin"), QStringLiteral("serialized"), false);
        QVERIFY(preview);
        auto pushing = std::async(std::launch::async, [&] { return service.executePush(repository, preview.value()); });
        const QString started = QDir(hooks).filePath(QStringLiteral("push-started"));
        QElapsedTimer timer; timer.start();
        while (!QFileInfo::exists(started) && timer.elapsed() < 8000)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        QVERIFY(QFileInfo::exists(started));
        auto staging = std::async(std::launch::async, [&] { return service.stage(repository, {QStringLiteral("pending.txt")}); });
        QVERIFY(staging.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout);
        QFile release(QDir(hooks).filePath(QStringLiteral("release-push")));
        QVERIFY(release.open(QIODevice::WriteOnly)); release.write("go\n"); release.close();
        const auto pushed = pushing.get();
        const auto staged = staging.get();
        QVERIFY(pushed);
        QCOMPARE(pushed.value().outcome, application::git::GitPushOutcome::Published);
        QVERIFY(staged);
    }

    void windowsManagedServiceStopsSpawnedChildren()
    {
#ifdef Q_OS_WIN
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
        QVERIFY(!powershell.isEmpty());
        const QString pidFile = root.filePath(QStringLiteral("child.pid"));
        QString escapedPidFile = pidFile; escapedPidFile.replace(QLatin1Char('\''), QStringLiteral("''"));
        QString escapedPowerShell = powershell; escapedPowerShell.replace(QLatin1Char('\''), QStringLiteral("''"));
        const QString script = QStringLiteral("$child = Start-Process -FilePath '%1' -ArgumentList '-NoProfile -NonInteractive -Command Start-Sleep -Seconds 60' -PassThru -WindowStyle Hidden; "
            "[IO.File]::WriteAllText('%2', [string]$child.Id); Start-Sleep -Seconds 60").arg(escapedPowerShell, escapedPidFile);
        domain::services::ServiceDefinition definition;
        definition.program = powershell;
        definition.arguments = {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script};
        definition.workingDirectory = root.path();
        application::services::QProcessManagedProcess process(definition);
        QSignalSpy started(&process, &application::services::IManagedProcess::started);
        QSignalSpy finished(&process, &application::services::IManagedProcess::finished);
        process.start();
        QTRY_VERIFY_WITH_TIMEOUT(started.size() == 1 && QFileInfo::exists(pidFile), 5000);
        QFile childPidFile(pidFile); QVERIFY(childPidFile.open(QIODevice::ReadOnly));
        bool pidOk = false; const DWORD childPid = childPidFile.readAll().trimmed().toUInt(&pidOk); QVERIFY(pidOk && childPid > 0);
        DWORD childOpenError = ERROR_SUCCESS;
        DWORD childWaitResult = WAIT_FAILED;
        auto childAlive = [childPid, &childOpenError, &childWaitResult] {
            HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, childPid);
            if (!child) { childOpenError = GetLastError(); return false; }
            childWaitResult = WaitForSingleObject(child, 0);
            CloseHandle(child); return childWaitResult == WAIT_TIMEOUT;
        };
        const bool childWasAlive = childAlive();
        process.stop(100);
        QTRY_VERIFY_WITH_TIMEOUT(finished.size() == 1, 5000);
        QVERIFY2(childWasAlive, qPrintable(QStringLiteral("Could not confirm the child was alive before stop (OpenProcess error %1, wait result %2).")
            .arg(childOpenError).arg(childWaitResult)));
        QTRY_VERIFY_WITH_TIMEOUT(!childAlive(), 3000);
#else
        QSKIP("Windows Job Object process-tree behavior is Windows-specific.");
#endif
    }

    void projectDatabasePageOpensRealSessionRepeatedly()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("project.sqlite"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        infrastructure::database::NativeSqliteQueryExecutor setup(control);
        QVERIFY(setup.open(path, DatabaseOpenMode::ReadWrite).isEmpty());
        QVERIFY(setup.run({QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY, value TEXT)"),
                          QStringLiteral("INSERT INTO things VALUES(1,'project row')")}, 0).message.isEmpty());
        setup.close();

        References references;
        references.filePath = path;
        infrastructure::database::QtSqlConnectionFactory factory;
        presentation::databases::DatabasePage page(references, factory);
        page.setWorkspace(QStringLiteral("workspace"), root.path());
        auto* open = button(page, QStringLiteral("Open Read-Only"));
        auto* close = button(page, QStringLiteral("Close"));
        auto* tree = page.findChild<QTreeView*>();
        auto* table = page.findChild<QTableView*>(QStringLiteral("databaseDataTable"));
        QVERIFY(open); QVERIFY(close); QVERIFY(tree); QVERIFY(table);
        QSignalSpy failures(&page, &presentation::databases::DatabasePage::databaseOperationFailed);
        const auto connections = QSqlDatabase::connectionNames();
        for (int attempt = 0; attempt < 3; ++attempt) {
            open->click();
            QTRY_COMPARE(tree->model()->rowCount(), 4);
            const auto tables = tree->model()->index(0, 0);
            QTRY_COMPARE(tree->model()->rowCount(tables), 1);
            tree->clicked(tree->model()->index(0, 0, tables));
            QTRY_COMPARE(table->model()->rowCount(), 1);
            QCOMPARE(table->model()->data(table->model()->index(0, 1)).toString(), QStringLiteral("project row"));
            QVERIFY(!(table->model()->flags(table->model()->index(0, 1)) & Qt::ItemIsEditable));
            // Project sessions must not open a second connection in Qt's separate engine.
            QCOMPARE(QSqlDatabase::connectionNames(), connections);
            close->click();
            QCOMPARE(table->model()->rowCount(), 0);
        }
        QCOMPARE(failures.count(), 0);
    }

    void readOnlyBackupRestoresWriteProtectionAfterSuccessAndFailure()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("project.sqlite"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        infrastructure::database::NativeSqliteQueryExecutor setup(control);
        QVERIFY(setup.open(path, DatabaseOpenMode::ReadWrite).isEmpty());
        QVERIFY(setup.run({QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY)"),
                          QStringLiteral("INSERT INTO things VALUES(1)")}, 0).message.isEmpty());
        setup.close();
        infrastructure::database::QtSqlDatabaseWorker worker(QStringLiteral("read-only-backup"));
        QSignalSpy opened(&worker, &infrastructure::database::QtSqlDatabaseWorker::openFinished);
        QSignalSpy backups(&worker, &infrastructure::database::QtSqlDatabaseWorker::backupFinished);
        QSignalSpy queries(&worker, &infrastructure::database::QtSqlDatabaseWorker::queryReady);
        worker.open(path, DatabaseOpenMode::ReadOnly);
        QVERIFY(opened.first().first().toBool());
        const auto backup = root.filePath(QStringLiteral("backup.sqlite"));
        worker.backup(backup);
        QVERIFY2(backups.last().first().toBool(), qPrintable(backups.last().at(1).toString()));
        QVERIFY(setup.open(backup, DatabaseOpenMode::ReadOnly).isEmpty());
        QCOMPARE(setup.run({QStringLiteral("SELECT COUNT(*) FROM things")}, 0).rows.first().first().toInt(), 1);
        setup.close();
        worker.executeQuery(QStringLiteral("DELETE FROM things"), false);
        QVERIFY(!qvariant_cast<DatabaseQueryResult>(queries.last().first()).message.isEmpty());
        worker.backup(root.filePath(QStringLiteral("missing-directory/backup.sqlite")));
        QVERIFY(!backups.last().first().toBool());
        worker.executeQuery(QStringLiteral("PRAGMA query_only"), false);
        QCOMPARE(qvariant_cast<DatabaseQueryResult>(queries.last().first()).rows.first().first().toInt(), 1);
        worker.executeQuery(QStringLiteral("DELETE FROM things"), false);
        QVERIFY(!qvariant_cast<DatabaseQueryResult>(queries.last().first()).message.isEmpty());
        worker.executeQuery(QStringLiteral("SELECT COUNT(*) FROM things"), false);
        QCOMPARE(qvariant_cast<DatabaseQueryResult>(queries.last().first()).rows.first().first().toInt(), 1);
        worker.close();
        worker.open(root.filePath(QStringLiteral("missing.sqlite")), DatabaseOpenMode::ReadOnly);
        QVERIFY(!opened.last().first().toBool());
        QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("missing.sqlite"))));
        worker.close();
    }

    void nativeParametersPreserveSqliteStorageTypes()
    {
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        infrastructure::database::NativeSqliteQueryExecutor executor(control);
        QVERIFY(executor.open(QStringLiteral(":memory:"), DatabaseOpenMode::ReadWrite).isEmpty());
        const QByteArray blob = QByteArray::fromHex("00ff0100");
        const auto result = executor.run({QStringLiteral("SELECT typeof(?), ?, typeof(?), ?, typeof(?), ?, typeof(?), ?")},
            0, {3.25, 3.25, blob, blob, QVariant::fromValue<qint64>(9007199254740993LL),
                QVariant::fromValue<qint64>(9007199254740993LL), QVariant(QMetaType(QMetaType::QString)),
                QVariant(QMetaType(QMetaType::QString))});
        QVERIFY2(result.message.isEmpty(), qPrintable(result.message));
        QCOMPARE(result.rows.size(), 1);
        const auto row = result.rows.first();
        QCOMPARE(row.at(0).toString(), QStringLiteral("real"));
        QCOMPARE(row.at(1).toDouble(), 3.25);
        QCOMPARE(row.at(2).toString(), QStringLiteral("blob"));
        QCOMPARE(row.at(3).toByteArray(), blob);
        QCOMPARE(row.at(4).toString(), QStringLiteral("integer"));
        QCOMPARE(row.at(5).toLongLong(), 9007199254740993LL);
        QCOMPARE(row.at(6).toString(), QStringLiteral("null"));
        QVERIFY(row.at(7).isNull());
    }

    void pageSizesAndOffsetsAgree()
    {
        QTemporaryDir root;
        const auto path = root.filePath(QStringLiteral("test.sqlite"));
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        infrastructure::database::NativeSqliteQueryExecutor setup(control);
        QVERIFY(setup.open(path, DatabaseOpenMode::ReadWrite).isEmpty());
        QVERIFY(setup
                    .run({QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY, value TEXT)"),
                          QStringLiteral(
                              "WITH RECURSIVE n(x) AS (VALUES(0) UNION ALL SELECT x+1 FROM n WHERE "
                              "x<1099) INSERT INTO things SELECT x,'original' FROM n")},
                         0)
                    .message.isEmpty());
        setup.close();
        infrastructure::database::QtSqlDatabaseSession session;
        QSignalSpy opened(&session, &IDatabaseSession::openFinished);
        QSignalSpy pages(&session, &IDatabaseSession::pageReady);
        session.open(path, DatabaseOpenMode::ReadOnly);
        QVERIFY(opened.wait());
        QVERIFY(opened.first().first().toBool());
        for (const int size : {25, 50, 100, 250, 500})
        {
            pages.clear();
            session.requestPage(QStringLiteral("things"), size, size);
            QVERIFY(pages.wait());
            const auto page = qvariant_cast<DatabasePageResult>(pages.first().at(1));
            QCOMPARE(page.pageSize, size);
            QCOMPARE(page.rows.size(), size);
            QCOMPARE(page.rows.first().first().toInt(), size);
        }
    }

    void sqlTimeoutCancellationAndReadOnlySafety()
    {
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        infrastructure::database::NativeSqliteQueryExecutor executor(control);
        QVERIFY(executor.open(QStringLiteral(":memory:"), DatabaseOpenMode::ReadWrite).isEmpty());
        const QString expensive =
            QStringLiteral("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE "
                           "x<1000000000) SELECT sum(x) FROM n");
        const auto timed = executor.run({expensive}, 0, {}, 10000, 50);
        QVERIFY(timed.message.contains(QStringLiteral("execution limit")));
        QVERIFY(timed.durationMs < 2000);
        const auto timedScript = executor.run({QStringLiteral("CREATE TABLE before_limit(value INTEGER)"), expensive}, 0, {}, 10000, 50);
        QVERIFY(timedScript.message.contains(QStringLiteral("execution limit")));
        QVERIFY(executor.run({QStringLiteral("SELECT COUNT(*) FROM before_limit")}, 0).message.isEmpty());
        ++control->generation;
        const auto cancelled = executor.run({expensive}, 0);
        QCOMPARE(cancelled.message, QStringLiteral("SQL operation cancelled."));
        const auto result =
            executor.run({QStringLiteral("SELECT NULL, 42, 3.5, 'text', X'0102'")}, 1);
        QVERIFY(result.message.isEmpty());
        QCOMPARE(result.rows.size(), 1);
        QVERIFY(result.rows.first().at(0).isNull());
        QCOMPARE(result.rows.first().at(1).toInt(), 42);
        QCOMPARE(result.rows.first().at(4).toByteArray(), QByteArray::fromHex("0102"));
        QTemporaryDir root;
        const auto path = root.filePath(QStringLiteral("empty.sqlite"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QVERIFY(executor.open(path, DatabaseOpenMode::ReadOnly).isEmpty());
        QVERIFY(!executor.run({QStringLiteral("CREATE TABLE forbidden(id INTEGER)")}, 1)
                     .message.isEmpty());
        executor.close();
    }

    void sqliteNativeScriptParsingPreservesCommentsTriggersAndPartialResults()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("scripts.sqlite"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        infrastructure::database::QtSqlDatabaseWorker worker(QStringLiteral("script-parser-regression"), control);
        QSignalSpy opened(&worker, &infrastructure::database::QtSqlDatabaseWorker::openFinished);
        worker.open(path, DatabaseOpenMode::ReadWrite); QCOMPARE(opened.size(), 1); QVERIFY(opened.first().first().toBool());
        QSignalSpy queries(&worker, &infrastructure::database::QtSqlDatabaseWorker::queryReady);
        worker.executeQuery(QStringLiteral("SELECT 1/* ordinary comment */AS n"), false);
        auto result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first());
        QVERIFY2(result.message.isEmpty(), qPrintable(result.message)); QCOMPARE(result.rows.first().first().toInt(), 1);
        worker.executeQuery(QStringLiteral("SELECT 'a;b' AS \"c;d\"; -- trailing comment with ;"), false);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first());
        QVERIFY2(result.message.isEmpty(), qPrintable(result.message)); QCOMPARE(result.rows.first().first().toString(), QStringLiteral("a;b"));
        worker.executeQuery(QStringLiteral("SELECT 2; SELECT 3"), false);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first());
        QVERIFY(result.message.contains(QStringLiteral("multiple statements")));
        const QString triggerScript = QStringLiteral(
            "CREATE TABLE base(value TEXT); CREATE TABLE audit(value TEXT); "
            "CREATE TRIGGER audit_insert AFTER INSERT ON base BEGIN "
            "INSERT INTO audit VALUES('left;right'); INSERT INTO audit VALUES('complete'); END;");
        worker.executeQuery(triggerScript, true);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first()); QVERIFY2(result.message.isEmpty(), qPrintable(result.message));
        worker.executeQuery(QStringLiteral("INSERT INTO base VALUES('x'); SELECT COUNT(*) FROM audit"), true);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first());
        QVERIFY2(result.message.isEmpty(), qPrintable(result.message)); QCOMPARE(result.rows.first().first().toInt(), 2);
        worker.executeQuery(QStringLiteral("CREATE TABLE partial(value INTEGER UNIQUE); INSERT INTO partial VALUES(1); INSERT INTO partial VALUES(1);"), true);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first()); QVERIFY(!result.message.isEmpty());
        worker.executeQuery(QStringLiteral("SELECT COUNT(*) FROM partial"), false);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first()); QCOMPARE(result.rows.first().first().toInt(), 1);
        worker.executeQuery(QStringLiteral("CREATE TABLE parse_atomic(value); THIS IS INVALID;"), true);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first()); QVERIFY(!result.message.isEmpty());
        worker.executeQuery(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE name='parse_atomic'"), false);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first()); QCOMPARE(result.rows.first().first().toInt(), 0);
        worker.executeQuery(QStringLiteral("CREATE TABLE cancelled_script(value); INSERT INTO cancelled_script VALUES(1);"), true, 999);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first()); QCOMPARE(result.message, QStringLiteral("SQL operation cancelled."));
        worker.executeQuery(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE name='cancelled_script'"), false);
        result = qvariant_cast<DatabaseQueryResult>(queries.takeFirst().first()); QCOMPARE(result.rows.first().first().toInt(), 0);
        worker.close();
    }

    void gridPreservesLiteralNullEmptyTextAndExplicitSqlNull()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("null-values.sqlite"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        infrastructure::database::NativeSqliteQueryExecutor setup(control);
        QVERIFY(setup.open(path, DatabaseOpenMode::ReadWrite).isEmpty());
        QVERIFY(setup.run({QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY, value TEXT, number INTEGER)"),
            QStringLiteral("INSERT INTO things VALUES(1, NULL, 42), (2, 'old', 42), (3, 'old', 42)")}, 0).message.isEmpty());
        setup.close();
        infrastructure::database::QtSqlDatabaseWorker worker(QStringLiteral("null-regression"));
        worker.open(path, DatabaseOpenMode::ReadWrite);
        QSignalSpy pages(&worker, &infrastructure::database::QtSqlDatabaseWorker::pageReady);
        worker.loadPage(QStringLiteral("things"), 0, 25, {}, true);
        QCOMPARE(pages.size(), 1);
        presentation::databases::DatabaseTableModel model;
        QVector<DatabaseColumn> columns(3);
        columns[0].name = "id"; columns[0].declaredType = "INTEGER";
        columns[1].name = "value"; columns[1].declaredType = "TEXT";
        columns[2].name = "number"; columns[2].declaredType = "INTEGER";
        model.setPage(QStringLiteral("things"), qvariant_cast<DatabasePageResult>(pages.first().at(1)),
            columns);
        model.setEditMode(true);
        const QVariant sqlNull{QMetaType(QMetaType::QString)};
        QVERIFY(model.setData(model.index(0, 1), QStringLiteral(""), Qt::EditRole));
        QVERIFY(!model.data(model.index(0, 1), Qt::EditRole).isNull());
        for (const auto& text : {QStringLiteral("null"), QStringLiteral("Null"), QStringLiteral("NULL")}) {
            QVERIFY(model.setData(model.index(1, 1), text, Qt::EditRole));
            QCOMPARE(model.data(model.index(1, 1), Qt::EditRole).toString(), text);
            QVERIFY(!model.data(model.index(1, 1), Qt::EditRole).isNull());
        }
        QVERIFY(model.setData(model.index(2, 1), sqlNull, Qt::EditRole));
        QVERIFY(model.setData(model.index(2, 2), sqlNull, Qt::EditRole));
        QVERIFY(model.data(model.index(2, 2), Qt::EditRole).isNull());
        QVERIFY(model.data(model.index(1, 1), Qt::DisplayRole) != model.data(model.index(2, 1), Qt::DisplayRole));
        QVERIFY(!model.setData(model.index(0, 0), 99, Qt::EditRole));
        QSignalSpy commits(&worker, &infrastructure::database::QtSqlDatabaseWorker::commitFinished);
        worker.commit(model.pendingChanges()); QCOMPARE(commits.size(), 1); QVERIFY(commits.first().first().toBool());
        QSignalSpy queries(&worker, &infrastructure::database::QtSqlDatabaseWorker::queryReady);
        worker.executeQuery(QStringLiteral("SELECT value, number FROM things ORDER BY id"), false);
        const auto result = qvariant_cast<DatabaseQueryResult>(queries.first().first());
        QCOMPARE(result.rows.size(), 3);
        QVERIFY(!result.rows.at(0).at(0).isNull()); QCOMPARE(result.rows.at(0).at(0).toString(), QStringLiteral(""));
        QCOMPARE(result.rows.at(1).at(0).toString(), QStringLiteral("NULL"));
        QVERIFY(result.rows.at(2).at(0).isNull()); QVERIFY(result.rows.at(2).at(1).isNull());
        model.appendBlankRow();
        QVERIFY(model.setData(model.index(3, 0), 4, Qt::EditRole));
        QVERIFY(model.setData(model.index(3, 1), sqlNull, Qt::EditRole));
        const auto insert = model.pendingChanges().inserts.first();
        QVERIFY(insert.values.contains("value")); QVERIFY(insert.values.value("value").isNull());
        worker.close();
    }

    void numericGridValuesRoundTripWithoutPrecisionLoss()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("numeric.sqlite"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        infrastructure::database::NativeSqliteQueryExecutor setup(control);
        QVERIFY(setup.open(path, DatabaseOpenMode::ReadWrite).isEmpty());
        QVERIFY(setup.run({QStringLiteral("CREATE TABLE numbers(id INTEGER PRIMARY KEY, value NUMERIC)"),
            QStringLiteral("INSERT INTO numbers VALUES(1,0),(2,0),(3,0),(4,0),(5,0),(6,0)")}, 0).message.isEmpty());
        setup.close();
        infrastructure::database::QtSqlDatabaseWorker worker(QStringLiteral("numeric-grid-regression"));
        worker.open(path, DatabaseOpenMode::ReadWrite);
        QSignalSpy pages(&worker, &infrastructure::database::QtSqlDatabaseWorker::pageReady);
        worker.loadPage(QStringLiteral("numbers"), 0, 25, {}, true); QCOMPARE(pages.size(), 1);
        presentation::databases::DatabaseTableModel model;
        QVector<DatabaseColumn> columns(2); columns[0].name = QStringLiteral("id"); columns[0].declaredType = QStringLiteral("INTEGER");
        columns[0].primaryKeyOrder = 1; columns[1].name = QStringLiteral("value"); columns[1].declaredType = QStringLiteral("NUMERIC");
        model.setPage(QStringLiteral("numbers"), qvariant_cast<DatabasePageResult>(pages.first().at(1)), columns);
        model.setEditMode(true);
        QVERIFY(model.setData(model.index(0, 1), QStringLiteral("9007199254740993"), Qt::EditRole));
        QCOMPARE(model.pendingChanges().cellEdits.first().value.toLongLong(), qlonglong(9007199254740993LL));
        QVERIFY(model.setData(model.index(1, 1), QStringLiteral("9223372036854775807"), Qt::EditRole));
        QVERIFY(model.setData(model.index(2, 1), QStringLiteral("-9223372036854775808"), Qt::EditRole));
        QVERIFY(model.setData(model.index(3, 1), QStringLiteral("1.25"), Qt::EditRole));
        QVERIFY(model.setData(model.index(4, 1), QStringLiteral(""), Qt::EditRole));
        QVERIFY(!model.setData(model.index(4, 1), QStringLiteral("9223372036854775808"), Qt::EditRole));
        QVERIFY(!model.setData(model.index(4, 1), QStringLiteral("-9223372036854775809"), Qt::EditRole));
        QVERIFY(model.setData(model.index(5, 1), QStringLiteral("NULL"), Qt::EditRole));
        QVERIFY(model.data(model.index(5, 1), Qt::EditRole).isNull());
        QSignalSpy commits(&worker, &infrastructure::database::QtSqlDatabaseWorker::commitFinished);
        worker.commit(model.pendingChanges()); QCOMPARE(commits.size(), 1); QVERIFY2(commits.first().first().toBool(), qPrintable(commits.first().last().toString()));
        QSignalSpy queries(&worker, &infrastructure::database::QtSqlDatabaseWorker::queryReady);
        worker.executeQuery(QStringLiteral("SELECT value, typeof(value) FROM numbers ORDER BY id"), false);
        const auto stored = qvariant_cast<DatabaseQueryResult>(queries.first().first()); QCOMPARE(stored.rows.size(), 6);
        QCOMPARE(stored.rows.at(0).at(0).toLongLong(), qlonglong(9007199254740993LL));
        QCOMPARE(stored.rows.at(1).at(0).toLongLong(), std::numeric_limits<qlonglong>::max());
        QCOMPARE(stored.rows.at(2).at(0).toLongLong(), std::numeric_limits<qlonglong>::min());
        QCOMPARE(stored.rows.at(3).at(0).toDouble(), 1.25); QCOMPARE(stored.rows.at(3).at(1).toString(), QStringLiteral("real"));
        QCOMPARE(stored.rows.at(4).at(0).toString(), QString{}); QCOMPARE(stored.rows.at(4).at(1).toString(), QStringLiteral("text"));
        QVERIFY(stored.rows.at(5).at(0).isNull()); QCOMPARE(stored.rows.at(5).at(1).toString(), QStringLiteral("null"));
        worker.close();
    }

    void visualCommitsAndSqlQueriesShareFileChanges()
    {
        QTemporaryDir root;
        const auto path = root.filePath(QStringLiteral("test.sqlite"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
        infrastructure::database::NativeSqliteQueryExecutor setup(control);
        QVERIFY(setup.open(path, DatabaseOpenMode::ReadWrite).isEmpty());
        QVERIFY(setup
                    .run({QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY, value TEXT)"),
                          QStringLiteral("INSERT INTO things VALUES(1,'original')")},
                         0)
                    .message.isEmpty());
        setup.close();
        infrastructure::database::QtSqlDatabaseWorker worker(QStringLiteral("commit-regression"));
        worker.open(path, DatabaseOpenMode::ReadWrite);
        QSignalSpy commits(&worker, &infrastructure::database::QtSqlDatabaseWorker::commitFinished);
        PendingChanges changes;
        changes.tableName = QStringLiteral("things");
        changes.cellEdits.append(
            {{1}, QStringLiteral("value"), QStringLiteral("edited"), QStringLiteral("original")});
        worker.commit(changes);
        QCOMPARE(commits.size(), 1);
        QVERIFY(commits.first().first().toBool());
        QSignalSpy queries(&worker, &infrastructure::database::QtSqlDatabaseWorker::queryReady);
        worker.executeQuery(QStringLiteral("SELECT value FROM things"), false);
        QCOMPARE(qvariant_cast<DatabaseQueryResult>(queries.first().first())
                     .rows.first()
                     .first()
                     .toString(),
                 QStringLiteral("edited"));
        worker.executeQuery(QStringLiteral("UPDATE things SET value='external'"), false);
        QSignalSpy pages(&worker, &infrastructure::database::QtSqlDatabaseWorker::pageReady);
        worker.loadPage(QStringLiteral("things"), 0, 25, {}, true);
        QCOMPARE(
            qvariant_cast<DatabasePageResult>(pages.first().at(1)).rows.first().at(1).toString(),
            QStringLiteral("external"));
        commits.clear();
        worker.commit(changes);
        QVERIFY(!commits.first().first().toBool());
        QSignalSpy backups(&worker, &infrastructure::database::QtSqlDatabaseWorker::backupFinished);
        worker.backup(root.filePath(QStringLiteral("backup.sqlite")));
        QVERIFY(backups.first().first().toBool());
        worker.close();
    }

    void staleGridDeletesRefuseChangedMissingAndReplacedRows()
    {
        struct Outcome { bool setup = false; bool committed = false; qint64 remaining = -1; qsizetype fingerprintSize = 0; QString message; };
        auto runScenario = [](const QString& fileName, const QString& schema, const QString& insert,
                              const QStringList& externalSql, const QString& table,
                              const QVector<DatabaseColumn>& columns) {
            Outcome outcome;
            QTemporaryDir root; if (!root.isValid()) return outcome;
            const QString path = root.filePath(fileName);
            QFile file(path); if (!file.open(QIODevice::WriteOnly)) return outcome; file.close();
            auto control = std::make_shared<infrastructure::database::SqliteQueryControl>();
            infrastructure::database::NativeSqliteQueryExecutor setup(control);
            if (!setup.open(path, DatabaseOpenMode::ReadWrite).isEmpty()) return outcome;
            const auto initialized = setup.run({schema, insert}, 0); setup.close(); if (!initialized.message.isEmpty()) return outcome;
            infrastructure::database::QtSqlDatabaseWorker worker(QStringLiteral("delete-snapshot-") + fileName);
            QSignalSpy opened(&worker, &infrastructure::database::QtSqlDatabaseWorker::openFinished);
            worker.open(path, DatabaseOpenMode::ReadWrite);
            if (opened.isEmpty() || !opened.first().first().toBool()) return outcome;
            QSignalSpy pages(&worker, &infrastructure::database::QtSqlDatabaseWorker::pageReady);
            worker.loadPage(table, 0, 25, {}, true);
            if (pages.isEmpty()) { worker.close(); return outcome; }
            presentation::databases::DatabaseTableModel model;
            model.setPage(table, qvariant_cast<DatabasePageResult>(pages.first().at(1)), columns);
            model.setEditMode(true);
            model.deleteRows({model.index(0, 0)});
            const auto pending = model.pendingChanges();
            if (pending.deletes.isEmpty()) { worker.close(); return outcome; }
            outcome.fingerprintSize = pending.deletes.first().originalFingerprint.size();
            if (!externalSql.isEmpty()) {
                infrastructure::database::NativeSqliteQueryExecutor external(control);
                if (!external.open(path, DatabaseOpenMode::ReadWrite).isEmpty()) { worker.close(); return outcome; }
                const auto changed = external.run(externalSql, 0); external.close();
                if (!changed.message.isEmpty()) { worker.close(); return outcome; }
            }
            QSignalSpy commits(&worker, &infrastructure::database::QtSqlDatabaseWorker::commitFinished);
            worker.commit(pending);
            if (!commits.isEmpty()) { outcome.committed = commits.first().first().toBool(); outcome.message = commits.first().last().toString(); }
            QSignalSpy queries(&worker, &infrastructure::database::QtSqlDatabaseWorker::queryReady);
            worker.executeQuery(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table), false);
            if (!queries.isEmpty()) {
                const auto result = qvariant_cast<DatabaseQueryResult>(queries.first().first());
                if (!result.rows.isEmpty()) outcome.remaining = result.rows.first().first().toLongLong();
            }
            worker.close(); outcome.setup = true; return outcome;
        };
        QVector<DatabaseColumn> simple(2); simple[0].name = QStringLiteral("id"); simple[0].declaredType = QStringLiteral("INTEGER"); simple[0].primaryKeyOrder = 1;
        simple[1].name = QStringLiteral("value"); simple[1].declaredType = QStringLiteral("TEXT");
        const auto unchanged = runScenario(QStringLiteral("unchanged.sqlite"), QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY, value TEXT)"),
            QStringLiteral("INSERT INTO things VALUES(1,'original')"), {}, QStringLiteral("things"), simple);
        QVERIFY(unchanged.setup); QCOMPARE(unchanged.fingerprintSize, 32); QVERIFY2(unchanged.committed, qPrintable(unchanged.message)); QCOMPARE(unchanged.remaining, 0);
        const auto modified = runScenario(QStringLiteral("modified.sqlite"), QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY, value TEXT)"),
            QStringLiteral("INSERT INTO things VALUES(1,'original')"), {QStringLiteral("UPDATE things SET value='changed' WHERE id=1")}, QStringLiteral("things"), simple);
        QVERIFY(modified.setup); QVERIFY(!modified.committed); QCOMPARE(modified.remaining, 1);
        const auto replaced = runScenario(QStringLiteral("replaced.sqlite"), QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY, value TEXT)"),
            QStringLiteral("INSERT INTO things VALUES(1,'original')"), {QStringLiteral("DELETE FROM things WHERE id=1"), QStringLiteral("INSERT INTO things VALUES(1,'replacement')")}, QStringLiteral("things"), simple);
        QVERIFY(replaced.setup); QVERIFY(!replaced.committed); QCOMPARE(replaced.remaining, 1);
        const auto missing = runScenario(QStringLiteral("missing.sqlite"), QStringLiteral("CREATE TABLE things(id INTEGER PRIMARY KEY, value TEXT)"),
            QStringLiteral("INSERT INTO things VALUES(1,'original')"), {QStringLiteral("DELETE FROM things WHERE id=1")}, QStringLiteral("things"), simple);
        QVERIFY(missing.setup); QVERIFY(!missing.committed); QCOMPARE(missing.remaining, 0);
        QVector<DatabaseColumn> composite(3);
        composite[0].name = QStringLiteral("left_key"); composite[0].primaryKeyOrder = 1;
        composite[1].name = QStringLiteral("right_key"); composite[1].primaryKeyOrder = 2;
        composite[2].name = QStringLiteral("value"); composite[2].declaredType = QStringLiteral("TEXT");
        const auto compositeRow = runScenario(QStringLiteral("composite.sqlite"),
            QStringLiteral("CREATE TABLE things(left_key TEXT, right_key INTEGER, value TEXT, PRIMARY KEY(left_key,right_key))"),
            QStringLiteral("INSERT INTO things VALUES('a',7,'original')"), {}, QStringLiteral("things"), composite);
        QVERIFY(compositeRow.setup); QVERIFY(compositeRow.committed); QCOMPARE(compositeRow.remaining, 0);
    }

    void closingSessionInterruptsRunningQuery()
    {
        QTemporaryDir root;
        const auto path = root.filePath(QStringLiteral("empty.sqlite"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        auto session = std::make_unique<infrastructure::database::QtSqlDatabaseSession>();
        QSignalSpy opened(session.get(), &IDatabaseSession::openFinished);
        session->open(path, DatabaseOpenMode::ReadOnly);
        QVERIFY(opened.wait());
        session->requestQuery(
            QStringLiteral("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE "
                           "x<1000000000) SELECT sum(x) FROM n"));
        QTest::qWait(50);
        QElapsedTimer timer;
        timer.start();
        session.reset();
        QVERIFY(timer.elapsed() < 2000);
    }

    void gridEditEnablesCommitAndCancelledNavigationKeepsChanges()
    {
        References references;
        FakeFactory factory;
        presentation::databases::DatabasePage page(references, factory, nullptr, 25);
        page.setWorkspace(QStringLiteral("workspace"), QStringLiteral("root"));
        auto *open = button(page, QStringLiteral("Open Read-Only"));
        QVERIFY(open);
        open->click();
        auto *tree = page.findChild<QTreeView *>();
        QVERIFY(tree);
        QTRY_VERIFY(tree->model()->rowCount() > 0);
        tree->clicked(tree->model()->index(0, 0, tree->model()->index(0, 0)));
        auto *table = page.findChild<QTableView *>(QStringLiteral("databaseDataTable"));
        QVERIFY(table);
        QTRY_COMPARE(table->model()->rowCount(), 25);
        QVERIFY(table->model()->setData(table->model()->index(0, 1), QStringLiteral("changed"),
                                        Qt::EditRole));
        auto *commit = button(page, QStringLiteral("Commit Changes"));
        QVERIFY(commit);
        QVERIFY(commit->isEnabled());
        const int before = factory.session->pageRequests;
        answerDialog(QMessageBox::Cancel);
        button(page, QStringLiteral("Next"))->click();
        QCOMPARE(factory.session->pageRequests, before);
        QCOMPARE(table->model()->data(table->model()->index(0, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("changed"));
        answerDialog(QMessageBox::Cancel);
        button(page, QStringLiteral("Close"))->click();
        QVERIFY(!factory.session->wasClosed);
        answerDialog(QMessageBox::Discard);
        button(page, QStringLiteral("Next"))->click();
        QTRY_COMPARE(factory.session->pageRequests, before + 1);
        QTRY_COMPARE(table->model()->data(table->model()->index(0, 0), Qt::DisplayRole).toInt(),
                     25);
        QVERIFY(!commit->isEnabled());
    }

    void supportBundleHasValidCentralDirectory()
    {
        QTemporaryDir root(QDir::current().filePath(QStringLiteral("support-bundle-XXXXXX")));
        QVERIFY(root.isValid());
        SettingsRepository repository;
        application::settings::SettingsService settings(repository);
        shared::security::RedactionService redactor;
        application::support::SupportBundleBuilder builder(root.path(), settings, redactor);
        QFile log(root.filePath(QStringLiteral("devhub.log")));
        QVERIFY2(log.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(log.errorString()));
        const QByteArray planted = QByteArrayLiteral("PLANTED_ASSISTANT_TRANSCRIPT_SECRET_7f83c4");
        log.write("2026-10-10 INFO [devhub.assistant] transcript="); log.write(planted); log.write("\n");
        log.write("2026-10-10 INFO [devhub.codex.protocol] state="); log.write(planted); log.write("\n");
        log.write("2026-10-10 INFO [devhub.workspace] status=ready\n"); log.close();
        const auto path = root.filePath(QStringLiteral("support.zip"));
        QVERIFY(builder.write(path, {}, {QStringLiteral("Healthy"), QString::fromLatin1(planted),
            QStringLiteral("Codex state: ") + QString::fromLatin1(planted)}));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(!bytes.contains(planted));
        QVERIFY(builder.previewCategories({}).join(QLatin1Char('\n')).contains(QStringLiteral("Assistant transcripts, and Codex state")));
        const qsizetype central = bytes.indexOf(QByteArray::fromHex("504b0102"));
        QVERIFY(central >= 0);
        const auto u16 = [&bytes](qsizetype at)
        { return qFromLittleEndian<quint16>(bytes.constData() + at); };
        const auto u32 = [&bytes](qsizetype at)
        { return qFromLittleEndian<quint32>(bytes.constData() + at); };
        QCOMPARE(bytes.mid(central + 46, u16(central + 28)), QByteArray("about.txt"));
        QCOMPARE(u32(central + 42), 0U);
        const qsizetype next =
            central + 46 + u16(central + 28) + u16(central + 30) + u16(central + 32);
        QCOMPARE(bytes.mid(next, 4), QByteArray::fromHex("504b0102"));
        const auto fixture = qEnvironmentVariable("DEVHUB_SUPPORT_ZIP_FIXTURE");
        if (!fixture.isEmpty())
        {
            QVERIFY(QFile::copy(path, fixture));
        }
    }
};

QTEST_MAIN(FeatureRegressionTest)
#include "feature_regression_test.moc"
