#include "../src/infrastructure/database/InternalDatabaseManager.h"

#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using devhub::infrastructure::database::InternalDatabaseManager;

class DatabaseMigrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void cleanDatabaseReceivesInitialSchema()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        InternalDatabaseManager manager(temporaryDirectory.filePath(QStringLiteral("data/devhub.db")),
                                        temporaryDirectory.filePath(QStringLiteral("backups")));
        QVERIFY(manager.open());
        QVERIFY(manager.migrate());
        QCOMPARE(manager.currentSchemaVersion(), manager.latestSchemaVersion());

        QSqlQuery query(manager.database());
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
            "('accounts', 'workspaces', 'service_definitions', 'log_sources', 'database_references', "
            "'note_nodes', 'note_tags', 'notifications', 'ui_state')")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 9);
    }

    void currentDatabaseIsUnchanged()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        InternalDatabaseManager manager(temporaryDirectory.filePath(QStringLiteral("devhub.db")),
                                        temporaryDirectory.filePath(QStringLiteral("backups")));
        QVERIFY(manager.open());
        QVERIFY(manager.migrate());
        const int firstVersion = manager.currentSchemaVersion();
        QSqlQuery query(manager.database());
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations")));
        QVERIFY(query.next());
        const int firstMigrationCount = query.value(0).toInt();
        QVERIFY(manager.migrate());
        QCOMPARE(manager.currentSchemaVersion(), firstVersion);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), firstMigrationCount);
    }

    void failedMigrationRollsBackItsChanges()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString migrationsPath = temporaryDirectory.filePath(QStringLiteral("migrations"));
        QVERIFY(QDir().mkpath(migrationsPath));
        QFile migration(QDir(migrationsPath).filePath(QStringLiteral("0001_broken_migration.sql")));
        QVERIFY(migration.open(QIODevice::WriteOnly | QIODevice::Text));
        migration.write("CREATE TABLE should_rollback (id INTEGER PRIMARY KEY);\n"
                        "THIS IS NOT VALID SQL;\n");
        migration.close();

        InternalDatabaseManager manager(temporaryDirectory.filePath(QStringLiteral("devhub.db")),
                                        temporaryDirectory.filePath(QStringLiteral("backups")),
                                        migrationsPath);
        QVERIFY(manager.open());
        const auto result = manager.migrate();
        QVERIFY(!result);
        QSqlQuery query(manager.database());
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'should_rollback'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
    }

    void newerDatabaseSchemaIsRefused()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString databasePath = temporaryDirectory.filePath(QStringLiteral("devhub.db"));
        const QString connectionName = QStringLiteral("newer-schema-fixture");
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(databasePath);
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE schema_migrations(version INTEGER NOT NULL PRIMARY KEY, "
                "name TEXT NOT NULL UNIQUE, applied_at_utc TEXT NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral(
                "INSERT INTO schema_migrations(version, name, applied_at_utc) "
                "VALUES(99, 'future', '2099-01-01T00:00:00Z')")));
            database.close();
        }
        QSqlDatabase::removeDatabase(connectionName);

        InternalDatabaseManager manager(databasePath, temporaryDirectory.filePath(QStringLiteral("backups")));
        QVERIFY(manager.open());
        const auto result = manager.migrate();
        QVERIFY(!result);
        QCOMPARE(result.error().code, devhub::domain::errors::ErrorCode::SchemaTooNew);
        QCOMPARE(manager.currentSchemaVersion(), 99);
    }

    void corruptDatabaseIsReportedWithoutRecreation()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString databasePath = temporaryDirectory.filePath(QStringLiteral("devhub.db"));
        QFile corrupt(databasePath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("this is not a SQLite database");
        corrupt.close();
        const qint64 originalSize = QFileInfo(databasePath).size();

        InternalDatabaseManager manager(databasePath, temporaryDirectory.filePath(QStringLiteral("backups")));
        const auto result = manager.open();
        QVERIFY(!result);
        QCOMPARE(result.error().code, devhub::domain::errors::ErrorCode::DatabaseCorrupt);
        QCOMPARE(QFileInfo(databasePath).size(), originalSize);
    }
};

QTEST_GUILESS_MAIN(DatabaseMigrationTest)
#include "database_migration_test.moc"
