#include "../src/application/notes/NoteFolderExporter.h"
#include "../src/application/ports/PortMonitorService.h"
#include "../src/application/security/PasswordTaskService.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/infrastructure/filesystem/LocalProfileResetter.h"
#include "../src/presentation/notes/NotesPage.h"
#include "../src/shared/identifiers/IdGenerator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QSemaphore>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTreeWidget>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <thread>

using namespace devhub;
using namespace application::notes;
using infrastructure::database::InternalDatabaseManager;
using infrastructure::filesystem::LocalProfileResetter;
using shared::common::Result;

namespace
{
domain::errors::AppError ioError()
{
    return domain::errors::AppError::failure(domain::errors::ErrorCode::IoFailure,
                                             domain::errors::ErrorCategory::FileSystem,
                                             QStringLiteral("Injected write failure"), {}, {});
}

NoteNode node(QString id, QString title, QString parent = {},
              NoteNodeKind kind = NoteNodeKind::Note)
{
    NoteNode result;
    result.id = std::move(id);
    result.title = std::move(title);
    result.parentId = std::move(parent);
    result.kind = kind;
    result.workspaceId = QStringLiteral("workspace");
    return result;
}

class NotesRepository final : public INotesRepository
{
  public:
    QVector<NoteNode> nodes{node(QStringLiteral("note"), QStringLiteral("Note"))};
    QHash<QString, QString> contents{{QStringLiteral("note"), QStringLiteral("Original")}};
    bool failSave = false;
    Result<QVector<NoteNode>> list(const QString &, bool) override
    {
        return Result<QVector<NoteNode>>::success(nodes);
    }
    Result<NoteNode> create(const QString &, QString, QString, NoteNodeKind) override
    {
        return Result<NoteNode>::failure(ioError());
    }
    Result<void> rename(const QString &, const QString &, QString) override
    {
        return Result<void>::success();
    }
    Result<void> move(const QString &, const QString &, QString) override
    {
        return Result<void>::success();
    }
    Result<void> setPinned(const QString &, const QString &, bool) override
    {
        return Result<void>::success();
    }
    Result<void> setTags(const QString &, const QString &, QStringList) override
    {
        return Result<void>::success();
    }
    Result<QString> readContent(const QString &, const QString &id) override
    {
        return Result<QString>::success(contents.value(id));
    }
    Result<QString> readContentBounded(const QString& workspace, const QString& id, qint64 maximumBytes) override
    {
        auto value = readContent(workspace, id);
        if (!value) return value;
        if (value.value().toUtf8().size() > maximumBytes) return Result<QString>::failure({});
        return value;
    }
    Result<QString> saveContent(const QString &, const QString &id, const QString &text,
                                const QString &) override
    {
        if (failSave)
            return Result<QString>::failure(ioError());
        contents[id] = text;
        return Result<QString>::success(QString::fromLatin1(
            QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex()));
    }
    QString contentPath(const QString &, const QString &) const override
    {
        return {};
    }
    Result<void> setTrashed(const QString &, const QString &, bool) override
    {
        return Result<void>::success();
    }
    Result<void> permanentlyRemove(const QString &, const QString &) override
    {
        return Result<void>::success();
    }
};

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool movePath(const QString &from, const QString &to)
{
    return QFileInfo(from).isDir() ? QDir().rename(from, to) : QFile::rename(from, to);
}
} // namespace

class ReliabilityTest final : public QObject
{
    Q_OBJECT
  private slots:
    void failedSaveAndRecoveryKeepWorkspaceAndEditor_data()
    {
        QTest::addColumn<bool>("blockJournalFile");
        QTest::newRow("cannot-create-recovery-directory") << false;
        QTest::newRow("cannot-open-recovery-file") << true;
    }
    void failedSaveAndRecoveryKeepWorkspaceAndEditor()
    {
        QFETCH(bool, blockJournalFile);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString blocked = temp.filePath(QStringLiteral("blocked"));
        if (blockJournalFile)
            QVERIFY(QDir().mkpath(
                QDir(blocked).filePath(QStringLiteral(".recovery/account/workspace/note.md"))));
        else
            QVERIFY(writeFile(blocked, "not a directory"));
        NotesRepository repository;
        presentation::notes::NotesPage page(repository, QStringLiteral("account"), blocked);
        QVERIFY(page.setWorkspace(QStringLiteral("workspace")));
        auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("notesTree"));
        auto *editor = page.findChild<QPlainTextEdit *>(QStringLiteral("markdownEditor"));
        QVERIFY(tree && editor);
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QCOMPARE(editor->toPlainText(), QStringLiteral("Original"));
        editor->setPlainText(QStringLiteral("Pending edits"));
        repository.failSave = true;
        QVERIFY(!page.prepareForTransition());
        QVERIFY(!page.setWorkspace(QStringLiteral("another-workspace")));
        QCOMPARE(editor->toPlainText(), QStringLiteral("Pending edits"));
        QVERIFY(tree->currentItem());
        QCOMPARE(repository.contents.value(QStringLiteral("note")), QStringLiteral("Original"));
        repository.failSave = false;
        QVERIFY(page.setWorkspace(QStringLiteral("another-workspace")));
        QCOMPARE(repository.contents.value(QStringLiteral("note")),
                 QStringLiteral("Pending edits"));
        QVERIFY(editor->toPlainText().isEmpty());
    }

    void recoveryPreservesEditsAcrossWorkspaceTransition()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        NotesRepository repository;
        presentation::notes::NotesPage page(repository, QStringLiteral("account"), temp.path());
        QVERIFY(page.setWorkspace(QStringLiteral("workspace")));
        auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("notesTree"));
        auto *editor = page.findChild<QPlainTextEdit *>(QStringLiteral("markdownEditor"));
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        const QString edits = QStringLiteral("Unsaved notes \u2014 recover me");
        editor->setPlainText(edits);
        repository.failSave = true;
        QVERIFY(!page.flushPendingSave());
        QVERIFY(page.prepareForTransition());
        const QString journal =
            temp.filePath(QStringLiteral(".recovery/account/workspace/note.md"));
        QCOMPARE(readFile(journal), edits.toUtf8());
        QVERIFY(page.setWorkspace(QStringLiteral("another-workspace")));
        QVERIFY(editor->toPlainText().isEmpty());
        QCOMPARE(readFile(journal), edits.toUtf8());
        QCOMPARE(repository.contents.value(QStringLiteral("note")), QStringLiteral("Original"));
    }

    void successfulRetryRemovesRecovery()
    {
        QTemporaryDir temp;
        NotesRepository repository;
        presentation::notes::NotesPage page(repository, QStringLiteral("account"), temp.path());
        QVERIFY(page.setWorkspace(QStringLiteral("workspace")));
        auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("notesTree"));
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        page.findChild<QPlainTextEdit *>(QStringLiteral("markdownEditor"))
            ->setPlainText(QStringLiteral("New text"));
        repository.failSave = true;
        QVERIFY(page.prepareForTransition());
        const QString journal =
            temp.filePath(QStringLiteral(".recovery/account/workspace/note.md"));
        QVERIFY(QFileInfo::exists(journal));
        repository.failSave = false;
        QVERIFY(page.flushPendingSave());
        QVERIFY(!QFileInfo::exists(journal));
        QCOMPARE(repository.contents.value(QStringLiteral("note")), QStringLiteral("New text"));
    }

    void exportCollisions_data()
    {
        QTest::addColumn<QString>("first");
        QTest::addColumn<QString>("second");
        QTest::addColumn<bool>("folder");
        QTest::newRow("duplicate") << QStringLiteral("Plan") << QStringLiteral("Plan") << false;
        QTest::newRow("case") << QStringLiteral("Plan") << QStringLiteral("plan") << false;
        QTest::newRow("sanitized") << QStringLiteral("a:b") << QStringLiteral("a?b") << false;
        QTest::newRow("folder-file") << QStringLiteral("Plan") << QStringLiteral("Plan.md") << true;
        QTest::newRow("trailing-dot") << QStringLiteral("Plan") << QStringLiteral("Plan.") << false;
    }
    void exportCollisions()
    {
        QFETCH(QString, first);
        QFETCH(QString, second);
        QFETCH(bool, folder);
        QTemporaryDir temp;
        NotesRepository repository;
        repository.nodes = {
            node(QStringLiteral("root"), QStringLiteral("Export"), {}, NoteNodeKind::Folder),
            node(QStringLiteral("a"), first, QStringLiteral("root")),
            node(QStringLiteral("b"), second, QStringLiteral("root"),
                 folder ? NoteNodeKind::Folder : NoteNodeKind::Note)};
        auto result = exportNoteFolder(repository, QStringLiteral("workspace"),
                                       QStringLiteral("root"), temp.path());
        QVERIFY(!result);
        QVERIFY(result.error().userMessage.contains(QStringLiteral("Multiple note items")));
        QVERIFY(QDir(temp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }

    void exportCreatesTreeAndRefusesExistingDestination()
    {
        QTemporaryDir temp;
        NotesRepository repository;
        repository.nodes = {
            node(QStringLiteral("root"), QStringLiteral("Export"), {}, NoteNodeKind::Folder),
            node(QStringLiteral("child"), QStringLiteral("CON"), QStringLiteral("root"),
                 NoteNodeKind::Folder),
            node(QStringLiteral("a"), QStringLiteral("a:b"), QStringLiteral("child")),
            node(QStringLiteral("b"), QStringLiteral("Other"), QStringLiteral("root"))};
        repository.contents = {{QStringLiteral("a"), QStringLiteral("First content")},
                               {QStringLiteral("b"), QStringLiteral("Second content")}};
        auto result = exportNoteFolder(repository, QStringLiteral("workspace"),
                                       QStringLiteral("root"), temp.path());
        QVERIFY(result);
        QCOMPARE(readFile(temp.filePath(QStringLiteral("Export/_CON/a_b.md"))),
                 QByteArray("First content"));
        QCOMPARE(readFile(temp.filePath(QStringLiteral("Export/Other.md"))),
                 QByteArray("Second content"));
        repository.contents[QStringLiteral("b")] = QStringLiteral("Overwrite attempt");
        QVERIFY(!exportNoteFolder(repository, QStringLiteral("workspace"), QStringLiteral("root"),
                                  temp.path()));
        QCOMPARE(readFile(temp.filePath(QStringLiteral("Export/Other.md"))),
                 QByteArray("Second content"));
    }

    void preservationFailureRollback_data()
    {
        QTest::addColumn<bool>("failRestore");
        QTest::newRow("successful-rollback") << false;
        QTest::newRow("failed-rollback-keeps-archive") << true;
    }
    void preservationFailureRollback()
    {
        QFETCH(bool, failRestore);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto accountId = domain::account::AccountId::fromString(
            QStringLiteral("22222222-2222-4222-8222-222222222222"));
        QVERIFY(accountId);
        const QString users = temp.filePath(QStringLiteral("users"));
        const QString user = QDir(users).filePath(accountId.value().toString());
        QVERIFY(QDir().mkpath(user));
        QVERIFY(writeFile(QDir(user).filePath(QStringLiteral("note.md")), "valuable notes"));
        InternalDatabaseManager database(temp.filePath(QStringLiteral("devhub.db")),
                                         temp.filePath(QStringLiteral("backups")));
        QVERIFY(database.open());
        QVERIFY(database.migrate());
        LocalProfileResetter resetter(temp.path(), users, database,
                                      [&](const QString &from, const QString &to)
                                      {
                                          if (from == database.databaseFilePath())
                                              return false;
                                          if (failRestore && to == user)
                                              return false;
                                          return movePath(from, to);
                                      });
        const auto result = resetter.resetProfile(accountId.value());
        QVERIFY(!result);
        QDir recovery(temp.filePath(QStringLiteral("recovery")));
        const auto archives = recovery.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (failRestore)
        {
            QCOMPARE(archives.size(), 1);
            const QString archive = recovery.filePath(archives.first());
            QCOMPARE(readFile(QDir(archive).filePath(
                         QStringLiteral("user-%1/note.md").arg(accountId.value().toString()))),
                     QByteArray("valuable notes"));
            QVERIFY(result.error().userMessage.contains(archive));
            QVERIFY(!database.database().isOpen());
            QVERIFY(!QFileInfo::exists(user));
        }
        else
        {
            QVERIFY(archives.isEmpty());
            QCOMPARE(readFile(QDir(user).filePath(QStringLiteral("note.md"))),
                     QByteArray("valuable notes"));
            QVERIFY(database.database().isOpen());
            QCOMPARE(database.currentSchemaVersion(), database.latestSchemaVersion());
        }
    }

    void failedNewSchemaRollback_data()
    {
        QTest::addColumn<bool>("failRestore");
        QTest::newRow("restores-database-without-sidecars") << false;
        QTest::newRow("keeps-database-on-restore-failure") << true;
    }
    void failedNewSchemaRollback()
    {
        QFETCH(bool, failRestore);
        QTemporaryDir temp;
        const auto accountId = domain::account::AccountId::fromString(
            QStringLiteral("22222222-2222-4222-8222-222222222222"));
        QVERIFY(accountId);
        const QString migrations = temp.filePath(QStringLiteral("migrations"));
        QVERIFY(QDir().mkpath(migrations));
        const QString migration = QDir(migrations).filePath(QStringLiteral("0001_fixture.sql"));
        QVERIFY(writeFile(
            migration,
            "CREATE TABLE preserved (value TEXT); INSERT INTO preserved VALUES ('original');"));
        InternalDatabaseManager database(temp.filePath(QStringLiteral("devhub.db")),
                                         temp.filePath(QStringLiteral("backups")), migrations);
        QVERIFY(database.open());
        QVERIFY(database.migrate());
        const QString users = temp.filePath(QStringLiteral("users"));
        const QString user = QDir(users).filePath(accountId.value().toString());
        QVERIFY(QDir().mkpath(user));
        QVERIFY(writeFile(QDir(user).filePath(QStringLiteral("note.md")), "valuable notes"));
        QVERIFY(writeFile(migration, "INVALID SQL;"));
        LocalProfileResetter resetter(temp.path(), users, database,
                                      [&](const QString &from, const QString &to)
                                      {
                                          if (failRestore && to == database.databaseFilePath())
                                              return false;
                                          return movePath(from, to);
                                      });
        const auto result = resetter.resetProfile(accountId.value());
        QVERIFY(!result);
        QCOMPARE(readFile(QDir(user).filePath(QStringLiteral("note.md"))),
                 QByteArray("valuable notes"));
        const QDir recovery(temp.filePath(QStringLiteral("recovery")));
        if (failRestore)
        {
            const auto archives = recovery.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            QCOMPARE(archives.size(), 1);
            const QString archive = recovery.filePath(archives.first());
            QVERIFY(QFileInfo(QDir(archive).filePath(QStringLiteral("devhub.db"))).size() > 0);
            QVERIFY(result.error().userMessage.contains(archive));
            QVERIFY(!database.database().isOpen());
            QVERIFY(!QFileInfo::exists(database.databaseFilePath()));
            return;
        }
        QVERIFY(database.database().isOpen());
        QSqlQuery query(database.database());
        QVERIFY(query.exec(QStringLiteral("SELECT value FROM preserved")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("original"));
        QVERIFY(recovery.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }

    void portMonitorDestructionWaitsForProvider()
    {
        struct Provider final : application::ports::IPortProvider
        {
            QSemaphore entered, release;
            std::atomic_bool finished{false};
            Result<QVector<application::ports::PortRecord>> enumerate() override
            {
                entered.release();
                release.acquire();
                finished = true;
                return Result<QVector<application::ports::PortRecord>>::success({});
            }
        } provider;
        struct Inspector final : application::ports::IProcessInspector
        {
            Result<application::ports::ProcessDetails> inspect(quint32) override
            {
                return Result<application::ports::ProcessDetails>::failure(ioError());
            }
        } inspector;
        auto monitor =
            std::make_unique<application::ports::PortMonitorService>(provider, inspector);
        QVERIFY(monitor->refresh());
        const bool entered = provider.entered.tryAcquire(1, 5000);
        if (!entered)
            provider.release.release();
        QVERIFY(entered);
        std::thread release(
            [&]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                provider.release.release();
            });
        monitor.reset();
        const bool completedBeforeDestruction = provider.finished.load();
        release.join();
        QThreadPool::globalInstance()->waitForDone();
        QVERIFY(completedBeforeDestruction);
    }

    void shutdownWaitsForPasswordWorkerAndDropsCallback_data()
    {
        QTest::addColumn<int>("stage");
        QTest::newRow("hash") << 0;
        QTest::newRow("verify") << 1;
        QTest::newRow("rehash-check") << 2;
        QTest::newRow("rehash") << 3;
    }
    void shutdownWaitsForPasswordWorkerAndDropsCallback()
    {
        QFETCH(int, stage);
        struct Hasher final : application::security::PasswordHasher
        {
            int stage = 0;
            mutable QSemaphore entered, release;
            mutable std::atomic_bool finished{false};
            void block() const
            {
                entered.release();
                release.acquire();
                finished = true;
            }
            Result<domain::account::PasswordVerifier>
            hash(const application::security::SecurePasswordBuffer &) const override
            {
                if (stage == 0 || stage == 3)
                    block();
                return Result<domain::account::PasswordVerifier>::failure(ioError());
            }
            Result<bool> verify(const application::security::SecurePasswordBuffer &,
                                const domain::account::PasswordVerifier &) const override
            {
                if (stage == 1)
                    block();
                return Result<bool>::success(true);
            }
            Result<bool> needsRehash(const domain::account::PasswordVerifier &) const override
            {
                if (stage == 2)
                    block();
                return Result<bool>::success(stage == 3);
            }
        } hasher;
        hasher.stage = stage;
        shared::identifiers::UuidIdGenerator ids;
        auto scheduler = std::make_unique<application::tasks::TaskScheduler>(ids, 1);
        auto service =
            std::make_unique<application::security::PasswordTaskService>(*scheduler, hasher);
        bool callback = false;
        if (stage == 0)
        {
            (void)service->hashPassword(QStringLiteral("test password"),
                                        [&](auto) { callback = true; });
        }
        else
        {
            auto verifier =
                domain::account::PasswordVerifier::fromStorage(QStringLiteral("test verifier"), 1);
            QVERIFY(verifier);
            (void)service->verifyPassword(QStringLiteral("test password"),
                                          std::move(verifier).value(),
                                          [&](auto) { callback = true; });
        }
        const bool entered = hasher.entered.tryAcquire(1, 5000);
        if (!entered)
            hasher.release.release();
        QVERIFY(entered);
        std::thread release(
            [&]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                hasher.release.release();
            });
        service.reset();
        scheduler.reset();
        const bool completedBeforeDestruction = hasher.finished.load();
        release.join();
        QCoreApplication::processEvents();
        QVERIFY(completedBeforeDestruction);
        QVERIFY(!callback);
    }
};

QTEST_MAIN(ReliabilityTest)
#include "reliability_test.moc"
