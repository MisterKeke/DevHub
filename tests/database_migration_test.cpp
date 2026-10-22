#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/infrastructure/workspace/SqlWorkspaceRepository.h"
#include "../src/application/workspaces/WorkspaceManager.h"
#include "../src/shared/identifiers/IdGenerator.h"
#include "../src/shared/time/ApplicationClock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using devhub::infrastructure::database::InternalDatabaseManager;

// Qt Test discovers non-static slots through the meta-object system. Its assertion
// macros add branches that inflate cognitive complexity in these linear tests.
// NOLINTBEGIN(readability-convert-member-functions-to-static,readability-function-cognitive-complexity)
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
            "'note_nodes', 'note_tags', 'notifications', 'ui_state', 'assistant_conversations')")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 10);
        QCOMPARE(manager.currentSchemaVersion(), 11);
        QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(workspaces)")));
        bool hasArchiveTimestamp = false;
        while (query.next()) {
            hasArchiveTimestamp |= query.value(1).toString() == QStringLiteral("archived_at_utc");
        }
        QVERIFY(hasArchiveTimestamp);
    }

    void archivingPreservesWorkspaceOwnedDataAndIdentity()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        InternalDatabaseManager manager(temporaryDirectory.filePath(QStringLiteral("devhub.db")),
                                        temporaryDirectory.filePath(QStringLiteral("backups")));
        QVERIFY(manager.open());
        QVERIFY(manager.migrate());
        QSqlQuery query(manager.database());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO accounts(id, username_normalized, display_username, password_hash) VALUES('account-a','user','User','hash')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO workspaces(id, account_id, display_name, root_path, entered_path, canonical_root_path) VALUES('workspace-a','account-a','Project','C:/Project','C:/Project','C:/Project')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO workspace_settings(workspace_id, key, value) VALUES('workspace-a','theme','dark')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO note_nodes(id, workspace_id, title) VALUES('note-a','workspace-a','Note')")));
        QVERIFY(query.exec(QStringLiteral("UPDATE workspaces SET archived_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id='workspace-a' AND account_id='account-a' AND archived_at_utc IS NULL")));
        QVERIFY(query.exec(QStringLiteral("SELECT id, archived_at_utc FROM workspaces WHERE account_id='account-a' AND archived_at_utc IS NOT NULL")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("workspace-a"));
        QVERIFY(!query.value(1).toString().isEmpty());
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM workspace_settings WHERE workspace_id='workspace-a'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM note_nodes WHERE workspace_id='workspace-a'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec(QStringLiteral("UPDATE workspaces SET archived_at_utc=NULL WHERE id='workspace-a' AND account_id='account-a' AND archived_at_utc IS NOT NULL")));
        QVERIFY(query.exec(QStringLiteral("SELECT id FROM workspaces WHERE id='workspace-a' AND account_id='account-a' AND archived_at_utc IS NULL")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("workspace-a"));
    }

    void workspaceRepositoryArchivesAndRestoresWithinAccountScope()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        InternalDatabaseManager manager(temporaryDirectory.filePath(QStringLiteral("devhub.db")),
                                        temporaryDirectory.filePath(QStringLiteral("backups")));
        QVERIFY(manager.open());
        QVERIFY(manager.migrate());
        auto account = devhub::domain::account::AccountId::fromString(QStringLiteral("a1111111-1111-4111-8111-111111111111"));
        auto otherAccount = devhub::domain::account::AccountId::fromString(QStringLiteral("a2222222-2222-4222-8222-222222222222"));
        auto workspaceId = devhub::domain::workspace::WorkspaceId::fromString(QStringLiteral("b1111111-1111-4111-8111-111111111111"));
        QVERIFY(account && otherAccount && workspaceId);
        QSqlQuery query(manager.database());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO accounts(id, username_normalized, display_username, password_hash) VALUES('a1111111-1111-4111-8111-111111111111','user','User','hash')")));
        devhub::domain::workspace::Workspace workspace(workspaceId.value(), account.value(), QStringLiteral("Project"),
            {QStringLiteral("C:/Project"), QStringLiteral("C:/Project")}, devhub::domain::workspace::WorkspaceStatus::Ready,
            false, 0, QDateTime::currentDateTimeUtc());
        devhub::infrastructure::workspace::SqlWorkspaceRepository repository(manager);
        QVERIFY(repository.add(workspace));
        QVERIFY(!repository.archive(workspaceId.value(), otherAccount.value()));
        auto active = repository.listForAccount(account.value());
        QVERIFY(active);
        QCOMPARE(active.value().size(), 1);
        QVERIFY(repository.archive(workspaceId.value(), account.value()));
        active = repository.listForAccount(account.value());
        QVERIFY(active);
        QVERIFY(active.value().isEmpty());
        auto archived = repository.listArchivedForAccount(account.value());
        QVERIFY(archived);
        QCOMPARE(archived.value().size(), 1);
        QCOMPARE(archived.value().front().id(), workspaceId.value());
        QVERIFY(!repository.restore(workspaceId.value(), otherAccount.value()));
        QVERIFY(repository.restore(workspaceId.value(), account.value()));
        active = repository.listForAccount(account.value());
        QVERIFY(active);
        QCOMPARE(active.value().size(), 1);
        QCOMPARE(active.value().front().id(), workspaceId.value());
    }

    void addingAnArchivedFolderRestoresItsOriginalWorkspace()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        InternalDatabaseManager database(temporaryDirectory.filePath(QStringLiteral("devhub.db")),
                                         temporaryDirectory.filePath(QStringLiteral("backups")));
        QVERIFY(database.open());
        QVERIFY(database.migrate());
        auto account = devhub::domain::account::AccountId::fromString(QStringLiteral("a3333333-3333-4333-8333-333333333333"));
        QVERIFY(account);
        QSqlQuery query(database.database());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO accounts(id, username_normalized, display_username, password_hash) VALUES('a3333333-3333-4333-8333-333333333333','user','User','hash')")));
        devhub::infrastructure::workspace::SqlWorkspaceRepository repository(database);
        devhub::application::workspaces::WorkspaceValidator validator;
        devhub::shared::identifiers::UuidIdGenerator ids;
        devhub::shared::time::SystemApplicationClock clock;
        devhub::application::workspaces::WorkspaceManager manager(repository, validator, ids, clock, account.value());
        auto original = manager.add(QStringLiteral("Original name"), temporaryDirectory.path());
        QVERIFY(original);
        QVERIFY(manager.archive(original.value().id()));
        auto readded = manager.add(QStringLiteral("New name"), temporaryDirectory.path());
        QVERIFY(readded);
        QCOMPARE(readded.value().id(), original.value().id());
        QCOMPARE(readded.value().displayName(), QStringLiteral("Original name"));
        auto active = manager.list();
        QVERIFY(active);
        QCOMPARE(active.value().size(), 1);
        QCOMPARE(active.value().front().id(), original.value().id());
        auto archived = manager.listArchived();
        QVERIFY(archived);
        QVERIFY(archived.value().isEmpty());
    }

    void existingWorkspaceArchivingMigrationRemainsCompatible()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString databasePath = temporaryDirectory.filePath(QStringLiteral("devhub.db"));
        const QString backupsPath = temporaryDirectory.filePath(QStringLiteral("backups"));
        const QString migrationsPath = temporaryDirectory.filePath(QStringLiteral("migrations"));
        QVERIFY(QDir().mkpath(migrationsPath));

        // Reproduce the already-shipped migration ledger independently of the
        // current resource name, so renaming migration 8 cannot break startup.
        const QDir bundledMigrations(QStringLiteral(":/database/migrations"));
        const QStringList migrationFiles = bundledMigrations.entryList(
            {QStringLiteral("*.sql")}, QDir::Files);
        for (const QString& name : migrationFiles) {
            if (name.left(4).toInt() >= 8) {
                continue;
            }
            QVERIFY(QFile::copy(bundledMigrations.filePath(name),
                                QDir(migrationsPath).filePath(name)));
        }
        QFile migration(QDir(migrationsPath).filePath(
            QStringLiteral("0008_workspace_archiving.sql")));
        QVERIFY(migration.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray sql = "ALTER TABLE workspaces ADD COLUMN archived_at_utc TEXT;\n"
                               "CREATE INDEX idx_workspaces_account_archived "
                               "ON workspaces(account_id, archived_at_utc, display_name COLLATE NOCASE);\n";
        QCOMPARE(migration.write(sql), static_cast<qint64>(sql.size()));
        migration.close();
        {
            InternalDatabaseManager legacy(databasePath, backupsPath, migrationsPath);
            QVERIFY(legacy.open());
            QVERIFY(legacy.migrate());
            QSqlQuery query(legacy.database());
            QVERIFY(query.exec(QStringLiteral(
                "INSERT INTO workspaces(id, display_name, root_path, archived_at_utc) "
                "VALUES('existing-workspace', 'Existing project', 'C:/Project', '2026-10-20T00:00:00Z')")));
        }

        InternalDatabaseManager current(databasePath, backupsPath);
        QVERIFY(current.open());
        const auto result = current.migrate();
        QVERIFY2(result.hasValue(), result ? "" : qPrintable(result.error().technicalMessage));
        QCOMPARE(current.currentSchemaVersion(), current.latestSchemaVersion());
        QSqlQuery query(current.database());
        QVERIFY(query.exec(QStringLiteral("SELECT name FROM schema_migrations WHERE version = 8")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("workspace_archiving"));
        QVERIFY(query.exec(QStringLiteral(
            "SELECT display_name, archived_at_utc FROM workspaces WHERE id = 'existing-workspace'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("Existing project"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("2026-10-20T00:00:00Z"));
    }

    void plannerImportCopyMigrationPreservesExistingProvenance()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString databasePath = temporaryDirectory.filePath(QStringLiteral("devhub.db"));
        const QString backupsPath = temporaryDirectory.filePath(QStringLiteral("backups"));
        const QString migrationsPath = temporaryDirectory.filePath(QStringLiteral("migrations-v9"));
        QVERIFY(QDir().mkpath(migrationsPath));
        const QDir bundled(QStringLiteral(":/database/migrations"));
        for (const QString& name : bundled.entryList({QStringLiteral("*.sql")}, QDir::Files)) {
            if (name.left(4).toInt() <= 9)
                QVERIFY(QFile::copy(bundled.filePath(name), QDir(migrationsPath).filePath(name)));
        }
        {
            InternalDatabaseManager versionNine(databasePath, backupsPath, migrationsPath);
            QVERIFY(versionNine.open());
            QVERIFY(versionNine.migrate());
            QCOMPARE(versionNine.currentSchemaVersion(), 9);
            QSqlQuery seed(versionNine.database());
            QVERIFY(seed.exec(QStringLiteral(
                "INSERT INTO accounts(id,username_normalized,display_username,password_hash) "
                "VALUES('account-a','user-a','User A','hash')")));
            QVERIFY(seed.exec(QStringLiteral(
                "INSERT INTO workspaces(id,account_id,display_name,root_path,entered_path,canonical_root_path) "
                "VALUES('workspace-a','account-a','Workspace A','C:/WorkspaceA','C:/WorkspaceA','C:/WorkspaceA')")));
            QVERIFY(seed.exec(QStringLiteral(
                "INSERT INTO planner_tasks(id,workspace_id,title,is_completed,status,created_at_utc,updated_at_utc,source_type) "
                "VALUES('task-one','workspace-a','Imported task',0,'todo','2026-01-01T00:00:00Z','2026-01-01T00:00:00Z','somethingqt')")));
            QVERIFY(seed.exec(QStringLiteral(
                "INSERT INTO planner_import_records(task_id,workspace_id,source_type,source_profile_identity,source_task_id,source_revision,source_created_at,source_updated_at,imported_at_utc) "
                "VALUES('task-one','workspace-a','somethingqt','profile-one','9007199254740995','17','source-created','source-updated','imported-at')")));
        }

        InternalDatabaseManager current(databasePath, backupsPath);
        QVERIFY(current.open());
        QVERIFY(current.migrate());
        QCOMPARE(current.currentSchemaVersion(), 11);
        QSqlQuery verify(current.database());
        QVERIFY(verify.exec(QStringLiteral(
            "SELECT task_id,source_task_id,source_revision,source_created_at,source_updated_at,copy_number "
            "FROM planner_import_records WHERE workspace_id='workspace-a'")));
        QVERIFY(verify.next());
        QCOMPARE(verify.value(0).toString(), QStringLiteral("task-one"));
        QCOMPARE(verify.value(1).toString(), QStringLiteral("9007199254740995"));
        QCOMPARE(verify.value(2).toString(), QStringLiteral("17"));
        QCOMPARE(verify.value(3).toString(), QStringLiteral("source-created"));
        QCOMPARE(verify.value(4).toString(), QStringLiteral("source-updated"));
        QCOMPARE(verify.value(5).toInt(), 1);
        QVERIFY(!verify.next());
        QVERIFY(verify.exec(QStringLiteral(
            "INSERT INTO planner_tasks(id,workspace_id,title,is_completed,status,created_at_utc,updated_at_utc,source_type) "
            "VALUES('task-two','workspace-a','Second copy',0,'todo','2026-01-02T00:00:00Z','2026-01-02T00:00:00Z','somethingqt')")));
        QVERIFY(verify.exec(QStringLiteral(
            "INSERT INTO planner_import_records(task_id,workspace_id,source_type,source_profile_identity,source_task_id,imported_at_utc,copy_number) "
            "VALUES('task-two','workspace-a','somethingqt','profile-one','9007199254740995','imported-again',2)")));
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
        migration.write("CREATE TABLE assistant_conversations (id INTEGER PRIMARY KEY);\n"
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
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'assistant_conversations'")));
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
// NOLINTEND(readability-convert-member-functions-to-static,readability-function-cognitive-complexity)

QTEST_GUILESS_MAIN(DatabaseMigrationTest)
#include "database_migration_test.moc"
