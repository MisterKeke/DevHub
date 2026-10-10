#include "../src/infrastructure/assistant/SqlConversationRepository.h"
#include "../src/infrastructure/notes/NotesRepository.h"
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace devhub;

class AssistantConversationRepositoryTest final : public QObject
{
    Q_OBJECT
private:
    QString createAccount(infrastructure::database::InternalDatabaseManager& manager)
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSqlQuery query(manager.database()); query.prepare(QStringLiteral(
            "INSERT INTO accounts(id,username_normalized,display_username,password_hash) VALUES(?,?,?,?)"));
        query.addBindValue(id); query.addBindValue(QStringLiteral("fixture-%1").arg(id));
        query.addBindValue(QStringLiteral("Fixture")); query.addBindValue(QStringLiteral("not-a-real-hash"));
        if (!query.exec()) return {};
        return id;
    }
    application::assistant::AssistantConversationMetadata metadata(const QString& accountId, const QString& workspaceId = {})
    {
        application::assistant::AssistantConversationMetadata value;
        value.conversation.id = QUuid::createUuid(); value.conversation.title = QStringLiteral("Fixture title");
        value.conversation.context.workspaceId = workspaceId; value.conversation.context.canonicalRoot = QStringLiteral("C:/fixture/workspace");
        value.devhubAccountId = accountId; value.binding.threadId = QStringLiteral("thread-fixture");
        value.binding.accountFingerprint = QStringLiteral("opaque-account-fingerprint");
        value.binding.codexHome = QStringLiteral("C:/Users/fixture/.codex"); value.binding.scratchDirectory = QStringLiteral("C:/cache/assistant");
        value.binding.profileFingerprint = QStringLiteral("safe-profile-fingerprint");
        value.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-10-09T10:00:00.000Z"), Qt::ISODateWithMs);
        value.updatedAtUtc = value.createdAtUtc; return value;
    }
private slots:
    void accountScopedPreparedMetadataRoundTrips()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        infrastructure::database::InternalDatabaseManager db(temp.filePath(QStringLiteral("db.sqlite")), temp.filePath(QStringLiteral("backups")));
        auto opened = db.open(); QVERIFY2(opened, qPrintable(opened.error().technicalMessage));
        QVERIFY(db.migrate()); QCOMPARE(db.currentSchemaVersion(), 7);
        const auto accountId = createAccount(db); QVERIFY(!accountId.isEmpty());
        const auto parsed = domain::account::AccountId::fromString(accountId); QVERIFY(parsed);
        infrastructure::assistant::SqlConversationRepository repository(db, parsed.value());
        auto item = metadata(accountId);
        QVERIFY(!item.conversation.id.isNull()); QCOMPARE(item.devhubAccountId, parsed.value().toString());
        QVERIFY(!item.binding.threadId.isEmpty()); QVERIFY(!item.binding.accountFingerprint.isEmpty());
        QVERIFY(!item.binding.codexHome.isEmpty()); QVERIFY(!item.binding.profileFingerprint.isEmpty());
        QVERIFY(item.createdAtUtc.isValid()); QVERIFY(item.updatedAtUtc.isValid());
        auto saved = repository.save(item); QVERIFY2(saved, qPrintable(saved.error().technicalMessage));
        const auto listed = repository.list(accountId, 100); QVERIFY(listed); QCOMPARE(listed.value().size(), 1);
        QCOMPARE(listed.value().front().conversation.id, item.conversation.id);
        QCOMPARE(listed.value().front().binding.codexHomeKey.size(), 64);
        QVERIFY(listed.value().front().binding.codexHome.isEmpty());
        QCOMPARE(listed.value().front().createdAtUtc.timeSpec(), Qt::UTC);
        const auto wrongAccount = repository.list(QStringLiteral("another-local-account"), 100); QVERIFY(!wrongAccount);
        item.devhubAccountId = QStringLiteral("another-local-account"); QVERIFY(!repository.save(item));
    }
    void workspaceDeletionKeepsAssociationButRemovesWorkspaceAccess()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        infrastructure::database::InternalDatabaseManager db(temp.filePath(QStringLiteral("db.sqlite")), temp.filePath(QStringLiteral("backups")));
        auto opened = db.open(); QVERIFY2(opened, qPrintable(opened.error().technicalMessage)); QVERIFY(db.migrate());
        const auto accountId = createAccount(db); const auto parsed = domain::account::AccountId::fromString(accountId); QVERIFY(parsed);
        const QString workspaceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSqlQuery workspace(db.database()); workspace.prepare(QStringLiteral(
            "INSERT INTO workspaces(id,display_name,root_path,account_id,entered_path,canonical_root_path) VALUES(?,?,?,?,?,?)"));
        workspace.addBindValue(workspaceId); workspace.addBindValue(QStringLiteral("Fixture")); workspace.addBindValue(QStringLiteral("C:/fixture/workspace"));
        workspace.addBindValue(accountId); workspace.addBindValue(QStringLiteral("C:/fixture/workspace")); workspace.addBindValue(QStringLiteral("C:/fixture/workspace"));
        QVERIFY(workspace.exec());
        infrastructure::assistant::SqlConversationRepository repository(db, parsed.value());
        auto item = metadata(accountId, workspaceId); QVERIFY(repository.save(item));
        QSqlQuery remove(db.database()); remove.prepare(QStringLiteral("DELETE FROM workspaces WHERE id=? AND account_id=?"));
        remove.addBindValue(workspaceId); remove.addBindValue(accountId); QVERIFY(remove.exec()); QCOMPARE(remove.numRowsAffected(), 1);
        const auto listed = repository.list(accountId, 10); QVERIFY(listed); QCOMPARE(listed.value().size(), 1);
        QVERIFY(listed.value().front().conversation.context.workspaceId.isEmpty());
        QCOMPARE(listed.value().front().conversation.context.canonicalRoot, QStringLiteral("C:/fixture/workspace"));
    }
    void repositoryRejectsNonOwningThread()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        infrastructure::database::InternalDatabaseManager db(temp.filePath(QStringLiteral("db.sqlite")), temp.filePath(QStringLiteral("backups")));
        auto opened = db.open(); QVERIFY2(opened, qPrintable(opened.error().technicalMessage)); QVERIFY(db.migrate()); const auto accountId = createAccount(db);
        const auto parsed = domain::account::AccountId::fromString(accountId); QVERIFY(parsed);
        infrastructure::assistant::SqlConversationRepository repository(db, parsed.value());
        bool succeeded = true;
        QThread worker;
        QObject::connect(&worker, &QThread::started, &worker,
            [&] { succeeded = repository.list(accountId, 5).hasValue(); worker.quit(); }, Qt::DirectConnection);
        worker.start(); QVERIFY(worker.wait(5000)); QVERIFY(!succeeded);
    }
    void boundedNoteReadsEnforceAccountWorkspaceTrashAndByteOwnership()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        infrastructure::database::InternalDatabaseManager db(temp.filePath(QStringLiteral("db.sqlite")), temp.filePath(QStringLiteral("backups")));
        auto opened = db.open(); QVERIFY2(opened, qPrintable(opened.error().technicalMessage)); QVERIFY(db.migrate());
        const QString accountId = createAccount(db);
        const auto parsed = domain::account::AccountId::fromString(accountId); QVERIFY(parsed);
        const QString workspaceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSqlQuery workspace(db.database()); workspace.prepare(QStringLiteral(
            "INSERT INTO workspaces(id,display_name,root_path,account_id,entered_path,canonical_root_path) VALUES(?,?,?,?,?,?)"));
        workspace.addBindValue(workspaceId); workspace.addBindValue(QStringLiteral("Notes fixture"));
        workspace.addBindValue(QStringLiteral("C:/fixture/notes")); workspace.addBindValue(accountId);
        workspace.addBindValue(QStringLiteral("C:/fixture/notes")); workspace.addBindValue(QStringLiteral("C:/fixture/notes"));
        QVERIFY(workspace.exec());
        infrastructure::notes::NotesRepository repository(db, parsed.value(), temp.filePath(QStringLiteral("notes")));
        const auto created = repository.create(workspaceId, {}, QStringLiteral("Bounded note"), application::notes::NoteNodeKind::Note);
        QVERIFY(created);
        QVERIFY(repository.saveContent(workspaceId, created.value().id, QStringLiteral("bounded body"), {}));
        const auto bounded = repository.readContentBounded(workspaceId, created.value().id, 100);
        QVERIFY(bounded); QCOMPARE(bounded.value(), QStringLiteral("bounded body"));
        QVERIFY(!repository.readContentBounded(QUuid::createUuid().toString(QUuid::WithoutBraces), created.value().id, 100));
        QVERIFY(!repository.readContentBounded(workspaceId, created.value().id, 2));
        QVERIFY(repository.setTrashed(workspaceId, created.value().id, true));
        QVERIFY(!repository.readContentBounded(workspaceId, created.value().id, 100));
        const auto otherAccount = domain::account::AccountId::fromString(QUuid::createUuid().toString(QUuid::WithoutBraces)); QVERIFY(otherAccount);
        infrastructure::notes::NotesRepository wrongAccount(db, otherAccount.value(), temp.filePath(QStringLiteral("other-notes")));
        QVERIFY(!wrongAccount.readContentBounded(workspaceId, created.value().id, 100));
    }
};

QTEST_GUILESS_MAIN(AssistantConversationRepositoryTest)
#include "assistant_conversation_repository_test.moc"
