#include "infrastructure/assistant/SomethingTasksReader.h"
#include "support/SyntheticSomethingProfile.h"

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
using devhub::tests::assistant::SyntheticProfile;

class SomethingTasksReaderTest final : public QObject {
    Q_OBJECT
private slots:
    void defaultDescriptionIsEmptyText()
    {
        SyntheticProfile profile;
        QVERIFY(profile.created);
        QVERIFY(profile.addTask(1, QStringLiteral("Task without a description")));
        const auto result = SomethingTasksReader::read(profile.path, QStringLiteral("1"));
        QVERIFY2(result.success, qPrintable(result.message));
        const auto description = result.data.value(QStringLiteral("task")).toObject().value(QStringLiteral("description"));
        QVERIFY(description.isString());
        QVERIFY(description.toString().isEmpty());
    }

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
        // Final sigma and capital sigma case-fold to the same Unicode tag.
        // Escapes keep the fixture independent of MSVC's source code page.
        QVERIFY(profile.addTag(13, QStringLiteral("\u03c2")));

        const auto exact = SomethingTasksReader::find(profile.path, {QStringLiteral(" devhub ")},
            QStringLiteral("incomplete"), 0, 20);
        QVERIFY(exact.success);
        QCOMPARE(exact.data.value(QStringLiteral("tasks")).toArray().size(), 2);
        const auto conjunction = SomethingTasksReader::find(profile.path,
            {QStringLiteral("DEVHUB"), QStringLiteral("review"), QStringLiteral("\u03a3")},
            QStringLiteral("incomplete"), 0, 20);
        QVERIFY(conjunction.success);
        const auto rows = conjunction.data.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject().value(QStringLiteral("taskId")).toString(), QStringLiteral("13"));
        const auto absentConjunction = SomethingTasksReader::find(profile.path,
            {QStringLiteral("DevHub"), QStringLiteral("MissingTag")},
            QStringLiteral("incomplete"), 0, 20);
        QVERIFY(absentConjunction.success);
        QVERIFY(absentConjunction.data.value(QStringLiteral("tasks")).toArray().isEmpty());
        const auto complete = SomethingTasksReader::find(profile.path, {QStringLiteral("DevHub")},
            QStringLiteral("complete"), 0, 20);
        QVERIFY(complete.success);
        QCOMPARE(complete.data.value(QStringLiteral("tasks")).toArray().size(), 1);
        const auto oldTag = SomethingTasksReader::find(profile.path, {QStringLiteral("DevHub-old")},
            QStringLiteral("all"), 0, 20);
        QVERIFY(oldTag.success);
        const auto oldRows = oldTag.data.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(oldRows.size(), 1);
        QCOMPARE(oldRows.first().toObject().value(QStringLiteral("taskId")).toString(), QStringLiteral("11"));
    }

    void unicodeTagsUseQtCaseFolding_data()
    {
        QTest::addColumn<QString>("storedTag");
        QTest::addColumn<QString>("requestedTag");
        QTest::newRow("accented Latin") << QStringLiteral("\u00c5ngstr\u00f6m") << QStringLiteral("\u00e5NGSTR\u00d6M");
        QTest::newRow("Greek final sigma") << QStringLiteral("\u03c2") << QStringLiteral("\u03a3");
        QTest::newRow("German sharp S") << QStringLiteral("Stra\u00dfe") << QStringLiteral("STRA\u1e9eE");
    }

    void unicodeTagsUseQtCaseFolding()
    {
        QFETCH(QString, storedTag); QFETCH(QString, requestedTag);
        SyntheticProfile profile;
        QVERIFY(profile.created);
        QVERIFY(profile.addTask(1, QStringLiteral("Unicode tag")));
        QVERIFY(profile.addTask(2, QStringLiteral("Different exact tag")));
        QVERIFY(profile.addTag(1, storedTag));
        QVERIFY(profile.addTag(2, storedTag + QStringLiteral("-old")));
        const auto result = SomethingTasksReader::find(profile.path, {QStringLiteral(" ") + requestedTag + QStringLiteral(" ")},
            QStringLiteral("all"), 0, 20);
        QVERIFY2(result.success, qPrintable(result.message));
        const auto rows = result.data.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject().value(QStringLiteral("taskId")).toString(), QStringLiteral("1"));
        const auto detail = SomethingTasksReader::read(profile.path, QStringLiteral("1"), requestedTag);
        QVERIFY2(detail.success, qPrintable(detail.message));
        QCOMPARE(SomethingTasksReader::read(profile.path, QStringLiteral("2"), requestedTag).errorCode,
            QStringLiteral("task_not_found"));
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

    void busyProfileReturnsBoundedRetryableFailure_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::newRow("check") << QStringLiteral("check");
        QTest::newRow("find") << QStringLiteral("find");
        QTest::newRow("read") << QStringLiteral("read");
    }

    void busyProfileReturnsBoundedRetryableFailure()
    {
        QFETCH(QString, operation);
        SyntheticProfile profile;
        QVERIFY(profile.created);
        QVERIFY(profile.addTask(1, QStringLiteral("Synthetic task")));
        QVERIFY(profile.addTag(1, QStringLiteral("DevHub")));
        const auto lookup = [&] {
            if (operation == QStringLiteral("check")) return SomethingTasksReader::check(profile.path);
            if (operation == QStringLiteral("find")) return SomethingTasksReader::find(profile.path,
                {QStringLiteral("DevHub")}, QStringLiteral("all"), 0, 20);
            return SomethingTasksReader::read(profile.path, QStringLiteral("1"));
        };
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
            result = lookup();
            QVERIFY(lock.exec(QStringLiteral("ROLLBACK")));
        }
        SyntheticProfile::close(writer, connectionName);
        QCOMPARE(result.errorCode, QStringLiteral("database_busy"));
        const auto recovered = lookup();
        QVERIFY2(recovered.success, qPrintable(recovered.message));
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
