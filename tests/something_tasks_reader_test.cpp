#include "infrastructure/assistant/SomethingTasksReader.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using devhub::infrastructure::assistant::SomethingTasksReader;

namespace {
class SyntheticProfile final {
public:
    SyntheticProfile() : path(root.filePath(QStringLiteral("something.sqlite"))) { createValid(); }

    bool createValid()
    {
        const QString connectionName = QStringLiteral("something-tasks-fixture-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        if (!db.open()) return false;
        const QStringList schema{
            QStringLiteral("CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY,name TEXT NOT NULL,applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"),
            QStringLiteral("INSERT INTO schema_migrations(version,name) VALUES(1,'core tasks currencies preferences'),(2,'synthetic harmless extension')"),
            QStringLiteral("CREATE TABLE tasks(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT NOT NULL,description TEXT NOT NULL DEFAULT '',is_completed INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,due_date TEXT,priority TEXT NOT NULL,difficulty TEXT,revision INTEGER NOT NULL)"),
            QStringLiteral("CREATE TABLE tags(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL,name_normalized TEXT NOT NULL UNIQUE)"),
            QStringLiteral("CREATE TABLE task_tags(task_id INTEGER NOT NULL,tag_id INTEGER NOT NULL,PRIMARY KEY(task_id,tag_id))"),
            QStringLiteral("CREATE TABLE task_subtasks(id INTEGER PRIMARY KEY AUTOINCREMENT,task_id INTEGER NOT NULL,title TEXT NOT NULL,is_completed INTEGER NOT NULL,position INTEGER NOT NULL)")};
        bool ok = true;
        {
            QSqlQuery query(db);
            for (const auto& sql : schema) if (!query.exec(sql)) { ok = false; break; }
        }
        close(db, connectionName);
        return ok;
    }

    bool createBroken()
    {
        QFile::remove(path);
        const QString connectionName = QStringLiteral("something-tasks-broken-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        if (!db.open()) return false;
        bool ok = false;
        {
            QSqlQuery query(db);
            ok = query.exec(QStringLiteral("CREATE TABLE schema_migrations(version INTEGER,name TEXT)"))
                && query.exec(QStringLiteral("INSERT INTO schema_migrations VALUES(1,'not SomethingQt')"))
                && query.exec(QStringLiteral("CREATE TABLE tasks(id INTEGER PRIMARY KEY,title TEXT)"));
        }
        close(db, connectionName);
        return ok;
    }

    bool addTask(qint64 id, const QString& title, bool completed = false,
        const QString& description = {}, qint64 revision = 1, const QString& dueDate = {})
    {
        const QString connectionName = QStringLiteral("something-tasks-insert-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        if (!db.open()) return false;
        bool ok = false;
        {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("INSERT INTO tasks(id,title,description,is_completed,created_at,updated_at,due_date,priority,difficulty,revision) VALUES(?,?,?,?,?,?,?,?,?,?)"));
            query.addBindValue(id); query.addBindValue(title); query.addBindValue(description);
            query.addBindValue(completed ? 1 : 0); query.addBindValue(QStringLiteral("2026-10-01T00:00:00Z"));
            query.addBindValue(QStringLiteral("2026-10-02T00:00:00Z"));
            query.addBindValue(dueDate.isEmpty() ? QVariant() : QVariant(dueDate));
            query.addBindValue(QStringLiteral("high")); query.addBindValue(QStringLiteral("hard")); query.addBindValue(revision);
            ok = query.exec();
        }
        close(db, connectionName);
        return ok;
    }

    bool addTag(qint64 taskId, QString name)
    {
        name = name.trimmed();
        const QString folded = name.toCaseFolded();
        const QString connectionName = QStringLiteral("something-tasks-tag-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        if (!db.open()) return false;
        bool ok = false;
        {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("INSERT INTO tags(name,name_normalized) VALUES(?,?) ON CONFLICT(name_normalized) DO NOTHING"));
            query.addBindValue(name); query.addBindValue(folded);
            ok = query.exec();
            query.prepare(QStringLiteral("INSERT INTO task_tags(task_id,tag_id) SELECT ?,id FROM tags WHERE name_normalized=?"));
            query.addBindValue(taskId); query.addBindValue(folded);
            ok = ok && query.exec();
        }
        close(db, connectionName);
        return ok;
    }

    bool addSubtask(qint64 taskId, const QString& title, bool completed, int position)
    {
        const QString connectionName = QStringLiteral("something-tasks-subtask-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        if (!db.open()) return false;
        bool ok = false;
        {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("INSERT INTO task_subtasks(task_id,title,is_completed,position) VALUES(?,?,?,?)"));
            query.addBindValue(taskId); query.addBindValue(title); query.addBindValue(completed ? 1 : 0); query.addBindValue(position);
            ok = query.exec();
        }
        close(db, connectionName);
        return ok;
    }

    static void close(QSqlDatabase& db, const QString& name)
    {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }

    QTemporaryDir root;
    QString path;
};
}

class SomethingTasksReaderTest final : public QObject {
    Q_OBJECT
private slots:
    void exactCaseFoldedTagsAndAndSemantics()
    {
        SyntheticProfile profile;
        QVERIFY(profile.addTask(10, QStringLiteral("DevHub"), false));
        QVERIFY(profile.addTask(11, QStringLiteral("Old tag"), false));
        QVERIFY(profile.addTask(12, QStringLiteral("Complete"), true));
        QVERIFY(profile.addTask(13, QStringLiteral("Unicode"), false));
        QVERIFY(profile.addTag(10, QStringLiteral("DevHub")));
        QVERIFY(profile.addTag(11, QStringLiteral("DevHub-old")));
        QVERIFY(profile.addTag(12, QStringLiteral("DevHub")));
        QVERIFY(profile.addTag(13, QStringLiteral(" DevHub ")));
        QVERIFY(profile.addTag(13, QStringLiteral("Review")));
        QVERIFY(profile.addTag(13, QStringLiteral("Straße")));

        const auto exact = SomethingTasksReader::find(profile.path, {QStringLiteral(" devhub ")},
            QStringLiteral("incomplete"), 0, 20);
        QVERIFY(exact.success);
        QCOMPARE(exact.data.value(QStringLiteral("tasks")).toArray().size(), 2);
        const auto conjunction = SomethingTasksReader::find(profile.path,
            {QStringLiteral("DEVHUB"), QStringLiteral("review"), QStringLiteral("STRASSE")},
            QStringLiteral("incomplete"), 0, 20);
        QVERIFY(conjunction.success);
        const auto rows = conjunction.data.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject().value(QStringLiteral("taskId")).toString(), QStringLiteral("13"));
        const auto complete = SomethingTasksReader::find(profile.path, {QStringLiteral("DevHub")},
            QStringLiteral("complete"), 0, 20);
        QVERIFY(complete.success);
        QCOMPARE(complete.data.value(QStringLiteral("tasks")).toArray().size(), 1);
        QVERIFY(SomethingTasksReader::find(profile.path, {QStringLiteral("DevHub-old")},
            QStringLiteral("all"), 0, 20).success);
    }

    void boundedDetailLongTextSubtasksAndLargeIds()
    {
        SyntheticProfile profile;
        constexpr qint64 largeId = 9007199254740993LL;
        QVERIFY(profile.addTask(largeId, QStringLiteral("Large ID"), false,
            QString(9000, QLatin1Char('x')), largeId, QStringLiteral("2026-10-23")));
        QVERIFY(profile.addTag(largeId, QStringLiteral("DevHub")));
        for (int i = 0; i < 55; ++i)
            QVERIFY(profile.addSubtask(largeId, QStringLiteral("Subtask ") + QString::number(i)
                + QString(300, QLatin1Char('z')), i % 2 == 0, i));

        const auto result = SomethingTasksReader::read(profile.path, QString::number(largeId));
        QVERIFY(result.success);
        const auto task = result.data.value(QStringLiteral("task")).toObject();
        QCOMPARE(task.value(QStringLiteral("taskId")).toString(), QString::number(largeId));
        QCOMPARE(task.value(QStringLiteral("revision")).toString(), QString::number(largeId));
        QCOMPARE(task.value(QStringLiteral("dueDate")).toString(), QStringLiteral("2026-10-23"));
        QCOMPARE(task.value(QStringLiteral("description")).toString().size(), 8192);
        QVERIFY(task.value(QStringLiteral("descriptionTruncated")).toBool());
        QCOMPARE(task.value(QStringLiteral("subtasks")).toArray().size(), 50);
        QVERIFY(task.value(QStringLiteral("subtasksTruncated")).toBool());
        QVERIFY(task.value(QStringLiteral("subtasks")).toArray().first().toObject()
            .value(QStringLiteral("titleTruncated")).toBool());
    }

    void offsetPagesAreBoundedAndDeterministicallyOrdered()
    {
        SyntheticProfile profile;
        for (qint64 id = 1; id <= 25; ++id) {
            QVERIFY(profile.addTask(id, QStringLiteral("Task %1").arg(id)));
            QVERIFY(profile.addTag(id, QStringLiteral("DevHub")));
        }
        const auto first = SomethingTasksReader::find(profile.path, {QStringLiteral("DevHub")},
            QStringLiteral("all"), 0, 20);
        QVERIFY(first.success); QVERIFY(first.data.value(QStringLiteral("hasMore")).toBool());
        QCOMPARE(first.data.value(QStringLiteral("tasks")).toArray().size(), 20);
        QCOMPARE(first.data.value(QStringLiteral("tasks")).toArray().first().toObject()
            .value(QStringLiteral("taskId")).toString(), QStringLiteral("25"));
        const auto second = SomethingTasksReader::find(profile.path, {QStringLiteral("DevHub")},
            QStringLiteral("all"), 20, 20);
        QVERIFY(second.success); QVERIFY(!second.data.value(QStringLiteral("hasMore")).toBool());
        QCOMPARE(second.data.value(QStringLiteral("tasks")).toArray().size(), 5);
        QCOMPARE(second.data.value(QStringLiteral("tasks")).toArray().first().toObject()
            .value(QStringLiteral("taskId")).toString(), QStringLiteral("5"));
    }

    void missingIncompatibleAndReplacedProfilesAreRevalidated()
    {
        SyntheticProfile profile;
        QVERIFY(SomethingTasksReader::check(profile.path).success);
        const QString missing = profile.root.filePath(QStringLiteral("missing.sqlite"));
        QCOMPARE(SomethingTasksReader::check(missing).errorCode, QStringLiteral("profile_missing"));
        QVERIFY(!QFileInfo::exists(missing));
        const QString previous = profile.root.filePath(QStringLiteral("previous.sqlite"));
        QVERIFY(QFile::rename(profile.path, previous));
        QVERIFY(profile.createBroken());
        QCOMPARE(SomethingTasksReader::check(profile.path).errorCode, QStringLiteral("schema_incompatible"));
    }

    void readsAreReadOnlyAndPermissionFailuresAreActionable()
    {
        SyntheticProfile profile;
        QVERIFY(profile.addTask(7, QStringLiteral("Keep this title")));
        QVERIFY(profile.addTag(7, QStringLiteral("DevHub")));
        const auto result = SomethingTasksReader::find(profile.path, {QStringLiteral("DevHub")},
            QStringLiteral("all"), 0, 20);
        QVERIFY(result.success);
        const auto task = SomethingTasksReader::read(profile.path, QStringLiteral("7"));
        QVERIFY(task.success);
        QCOMPARE(task.data.value(QStringLiteral("task")).toObject().value(QStringLiteral("title")).toString(),
            QStringLiteral("Keep this title"));
        QVERIFY(SomethingTasksReader::check(profile.path).success);

        const auto originalPermissions = QFile::permissions(profile.path);
        QVERIFY(QFile::setPermissions(profile.path, originalPermissions
            & ~(QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther)));
        if (QFileInfo(profile.path).isReadable()) {
            QFile::setPermissions(profile.path, originalPermissions);
            QSKIP("This platform does not expose unreadable-file permissions to the test process.");
        }
        QCOMPARE(SomethingTasksReader::check(profile.path).errorCode, QStringLiteral("profile_unreadable"));
        QFile::setPermissions(profile.path, originalPermissions);
    }

    void busyProfileReturnsBoundedRetryableFailure()
    {
        SyntheticProfile profile;
        const QString connectionName = QStringLiteral("something-tasks-exclusive-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto writer = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        writer.setDatabaseName(profile.path);
        writer.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=50"));
        QVERIFY(writer.open());
        devhub::infrastructure::assistant::SomethingTasksReadResult result;
        {
            QSqlQuery lock(writer);
            QVERIFY(lock.exec(QStringLiteral("PRAGMA journal_mode=DELETE")));
            QVERIFY(lock.exec(QStringLiteral("BEGIN EXCLUSIVE")));
            result = SomethingTasksReader::check(profile.path);
            lock.exec(QStringLiteral("ROLLBACK"));
        }
        SyntheticProfile::close(writer, connectionName);
        QCOMPARE(result.errorCode, QStringLiteral("database_busy"));
    }

    void cancelledReadStopsBeforeQuerying()
    {
        SyntheticProfile profile;
        devhub::application::tasks::CancellationSource cancellation;
        cancellation.requestCancellation();
        const auto result = SomethingTasksReader::find(profile.path, {QStringLiteral("DevHub")},
            QStringLiteral("all"), 0, 20, cancellation.token());
        QVERIFY(result.cancelled);
        QCOMPARE(result.errorCode, QStringLiteral("cancelled"));
    }
};

QTEST_GUILESS_MAIN(SomethingTasksReaderTest)
#include "something_tasks_reader_test.moc"
