#include "application/services/ServiceCoordinator.h"
#include "application/settings/SettingsService.h"
#include "application/support/SupportBundleBuilder.h"
#include "infrastructure/database/NativeSqliteQueryExecutor.h"
#include "infrastructure/database/QtSqlDatabaseSession.h"
#include "infrastructure/database/QtSqlDatabaseWorker.h"
#include "presentation/databases/DatabasePage.h"

#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
#include <QtEndian>
#include <QtTest/QtTest>

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
    Result<QVector<DatabaseReference>> list(const QString &workspace) override
    {
        return Result<QVector<DatabaseReference>>::success(
            {{QStringLiteral("db"), workspace, QStringLiteral("Test"),
              QStringLiteral("unused.sqlite")}});
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
