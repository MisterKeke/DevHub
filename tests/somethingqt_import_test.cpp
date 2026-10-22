#include "../src/infrastructure/planner/SomethingQtImportReader.h"
#include "support/SyntheticSomethingProfile.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QtTest/QtTest>

using namespace devhub;
using devhub::infrastructure::planner::SomethingQtImportFilter;
using devhub::infrastructure::planner::SomethingQtImportReader;
using devhub::infrastructure::planner::SomethingQtImportSelection;
using devhub::tests::assistant::SyntheticProfile;

class SomethingQtImportTest final : public QObject
{
    Q_OBJECT
private slots:
    void emptyProfileCanBeBrowsedAndImported()
    {
        SyntheticProfile profile;
        QVERIFY(profile.created);
        const QByteArray before = fileHash(profile.path);
        const auto inspected = SomethingQtImportReader::inspect(profile.path);
        QVERIFY2(inspected.success, qPrintable(inspected.message));
        QCOMPARE(inspected.value.taskCount, qint64(0));
        QVERIFY(inspected.value.tags.isEmpty());

        const auto page = SomethingQtImportReader::listPage(profile.path, inspected.value.identity, {});
        QVERIFY2(page.success, qPrintable(page.message));
        QCOMPARE(page.value.totalCount, qint64(0));
        QVERIFY(page.value.tasks.isEmpty());

        SomethingQtImportSelection all;
        all.all = true;
        const auto count = SomethingQtImportReader::countSelection(profile.path, inspected.value.identity, all);
        QVERIFY2(count.success, qPrintable(count.message));
        QCOMPARE(count.value, qint64(0));
        int callbacks = 0;
        const auto stream = SomethingQtImportReader::forEachTask(profile.path, inspected.value.identity,
            all, {}, [&callbacks](domain::planner::PlannerTask) { ++callbacks; return true; });
        QVERIFY2(stream.success, qPrintable(stream.message));
        QCOMPARE(stream.sourceCount, qint64(0));
        QCOMPARE(callbacks, 0);
        QCOMPARE(fileHash(profile.path), before);
    }

    void fullPayloadPaginationAndReadOnlyGuarantee()
    {
        SyntheticProfile profile;
        QVERIFY(profile.created);
        constexpr qint64 wideId = 9007199254740995LL;
        constexpr qint64 wideRevision = 9007199254740997LL;
        const QString description = QStringLiteral("開始 — πλήρες — ") + QString(120000, QChar(0x03bb));
        QVERIFY(profile.addTask(wideId, QStringLiteral("Import 東京 🛰️"), false,
            description, wideRevision, QStringLiteral("2027-04-29")));
        QVERIFY(profile.addTask(9007199254740996LL, QStringLiteral("Completed task"), true,
            QStringLiteral("Done"), 8));
        QVERIFY(profile.addTag(wideId, QStringLiteral("研究")));
        QVERIFY(profile.addTag(wideId, QStringLiteral("واجهة")));
        QVERIFY(profile.addTag(wideId, QStringLiteral("🧪 Unicode")));
        QVERIFY(profile.addTag(9007199254740996LL, QStringLiteral("complete")));
        for (int position = 74; position >= 0; --position)
            QVERIFY(profile.addSubtask(wideId, QStringLiteral("子タスク %1").arg(position), position % 2 == 0, position));

        const QByteArray before = fileHash(profile.path);
        QVERIFY(!before.isEmpty());
        const auto inspected = SomethingQtImportReader::inspect(profile.path);
        QVERIFY2(inspected.success, qPrintable(inspected.message));
        QCOMPARE(inspected.value.taskCount, qint64(2));
        QVERIFY(inspected.value.tags.contains(QStringLiteral("研究")));
        QCOMPARE(inspected.value.identity, SomethingQtImportReader::inspect(profile.path).value.identity);

        SomethingQtImportFilter filter;
        filter.searchText = QStringLiteral("東京");
        filter.tag = QStringLiteral("研究");
        filter.priority = QStringLiteral("high");
        filter.completion = 0;
        const auto page = SomethingQtImportReader::listPage(profile.path, inspected.value.identity, filter);
        QVERIFY2(page.success, qPrintable(page.message));
        QCOMPARE(page.value.totalCount, qint64(1));
        QCOMPARE(page.value.tasks.size(), 1);
        QCOMPARE(page.value.tasks.first().sourceTaskId, QString::number(wideId));
        QCOMPARE(page.value.tasks.first().tags.size(), 3);
        QCOMPARE(page.value.tasks.first().revision, QString::number(wideRevision));

        const auto full = SomethingQtImportReader::readTask(profile.path, inspected.value.identity, QString::number(wideId));
        QVERIFY2(full.success, qPrintable(full.message));
        QCOMPARE(full.value.sourceTaskId, QString::number(wideId));
        QCOMPARE(full.value.sourceRevision, QString::number(wideRevision));
        QCOMPARE(full.value.description, description);
        QCOMPARE(full.value.dueDate, QDate(2027, 4, 29));
        QCOMPARE(full.value.status, domain::planner::PlannerStatus::Todo);
        QCOMPARE(full.value.tags.size(), 3);
        QCOMPARE(full.value.subtasks.size(), 75);
        QCOMPARE(full.value.subtasks.first().position, 0);
        QCOMPARE(full.value.subtasks.last().position, 74);
        QCOMPARE(full.value.subtasks.first().title, QStringLiteral("子タスク 0"));
        QCOMPARE(full.value.subtasks.first().completed, true);
        QVERIFY(!full.value.subtasks.first().sourceSubtaskId.isEmpty());

        const auto completed = SomethingQtImportReader::readTask(profile.path, inspected.value.identity,
            QStringLiteral("9007199254740996"));
        QVERIFY(completed.success);
        QCOMPARE(completed.value.status, domain::planner::PlannerStatus::Done);
        QVERIFY(completed.value.completed);
        QVERIFY(!completed.value.dueDate.isValid());

        SomethingQtImportSelection byTag;
        byTag.tags = {QStringLiteral("研究"), QStringLiteral("missing")};
        const auto expected = SomethingQtImportReader::countSelection(profile.path, inspected.value.identity, byTag);
        QVERIFY(expected.success);
        QCOMPARE(expected.value, qint64(1));
        int streamed = 0;
        const auto stream = SomethingQtImportReader::forEachTask(profile.path, inspected.value.identity, byTag, {},
            [&streamed, &description](domain::planner::PlannerTask task) {
                ++streamed;
                return task.description == description && task.subtasks.size() == 75;
            });
        QVERIFY2(stream.success, qPrintable(stream.message));
        QCOMPARE(stream.sourceCount, qint64(1));
        QCOMPARE(streamed, 1);

        application::tasks::CancellationSource cancellation;
        cancellation.requestCancellation();
        const auto cancelled = SomethingQtImportReader::inspect(profile.path, cancellation.token());
        QVERIFY(cancelled.cancelled);
        QCOMPARE(cancelled.errorCode, QStringLiteral("cancelled"));
        QCOMPARE(fileHash(profile.path), before);
    }

    void selectionValidatesBeforeStreamingAndIdentityMustMatch()
    {
        SyntheticProfile profile;
        QVERIFY(profile.addTask(101, QStringLiteral("Present")));
        const auto profileInfo = SomethingQtImportReader::inspect(profile.path);
        QVERIFY(profileInfo.success);
        const auto changed = SomethingQtImportReader::listPage(profile.path,
            profileInfo.value.identity + QStringLiteral("-different"), {});
        QVERIFY(!changed.success);
        QCOMPARE(changed.errorCode, QStringLiteral("profile_changed"));

        SomethingQtImportSelection selected;
        selected.sourceTaskIds = {QStringLiteral("101"), QStringLiteral("999")};
        int callbackCount = 0;
        const auto stream = SomethingQtImportReader::forEachTask(profile.path, profileInfo.value.identity,
            selected, {}, [&callbackCount](domain::planner::PlannerTask) { ++callbackCount; return true; });
        QVERIFY(!stream.success);
        QCOMPARE(stream.errorCode, QStringLiteral("task_missing"));
        QCOMPARE(callbackCount, 0);
    }

    void compatibleLiveWalProfileRemainsInWalMode()
    {
        SyntheticProfile profile;
        QVERIFY(profile.created);
        const QString connectionName = QStringLiteral("devhub-import-wal-writer-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto writer = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        writer.setDatabaseName(profile.path);
        QVERIFY(writer.open());
        {
            QSqlQuery journal(writer);
            QVERIFY(journal.exec(QStringLiteral("PRAGMA journal_mode=WAL")));
            QVERIFY(journal.next());
            QCOMPARE(journal.value(0).toString().toCaseFolded(), QStringLiteral("wal"));
        }
        QSqlQuery addTask(writer);
        addTask.prepare(QStringLiteral("INSERT INTO tasks(id,title,description,is_completed,created_at,updated_at,due_date,priority,difficulty,revision) VALUES(?,?,?,?,?,?,?,?,?,?)"));
        addTask.addBindValue(503);
        addTask.addBindValue(QStringLiteral("Live SomethingQt task"));
        addTask.addBindValue(QStringLiteral(""));
        addTask.addBindValue(0);
        addTask.addBindValue(QStringLiteral("2026-10-01T00:00:00Z"));
        addTask.addBindValue(QStringLiteral("2026-10-02T00:00:00Z"));
        addTask.addBindValue(QVariant());
        addTask.addBindValue(QStringLiteral("normal"));
        addTask.addBindValue(QStringLiteral("medium"));
        addTask.addBindValue(1);
        QVERIFY2(addTask.exec(), qPrintable(addTask.lastError().text()));

        const auto read = SomethingQtImportReader::inspect(profile.path);
        QVERIFY2(read.success, qPrintable(read.message));
        QCOMPARE(read.value.taskCount, qint64(1));
        QSqlQuery unchangedJournal(writer);
        QVERIFY(unchangedJournal.exec(QStringLiteral("PRAGMA journal_mode")));
        QVERIFY(unchangedJournal.next());
        QCOMPARE(unchangedJournal.value(0).toString().toCaseFolded(), QStringLiteral("wal"));
        addTask = QSqlQuery(); unchangedJournal = QSqlQuery();
        writer.close(); writer = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    void incompatibleProfileIsRejected()
    {
        SyntheticProfile profile;
        QVERIFY(profile.createBroken());
        const auto result = SomethingQtImportReader::inspect(profile.path);
        QVERIFY(!result.success);
        QCOMPARE(result.errorCode, QStringLiteral("schema_incompatible"));
    }

private:
    static QByteArray fileHash(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    }
};

QTEST_GUILESS_MAIN(SomethingQtImportTest)
#include "somethingqt_import_test.moc"
