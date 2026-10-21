#pragma once

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>

namespace devhub::tests::assistant {
class SyntheticProfile final {
public:
    SyntheticProfile() : path(root.filePath(QStringLiteral("something.sqlite"))) { created = createValid(); }

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
            query.addBindValue(id); query.addBindValue(title);
            // A default-constructed QString is null; SQL requires an empty TEXT value.
            query.addBindValue(description.isNull() ? QStringLiteral("") : description);
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
    bool created = false;
};
}

