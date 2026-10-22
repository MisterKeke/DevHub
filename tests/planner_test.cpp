#include "../src/application/planner/PlannerService.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/infrastructure/planner/SqlPlannerRepository.h"
#include "../src/shared/identifiers/IdGenerator.h"
#include "../src/shared/time/ApplicationClock.h"

#include <QDir>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>

using namespace devhub;

// NOLINTBEGIN(readability-convert-member-functions-to-static,readability-function-cognitive-complexity)
class PlannerTest final : public QObject
{
    Q_OBJECT

  private slots:
    void init()
    {
        m_temporary.reset(
            new QTemporaryDir(QDir::current().filePath(QStringLiteral("planner-tests-XXXXXX"))));
        QVERIFY(m_temporary->isValid());
        m_database.reset(new infrastructure::database::InternalDatabaseManager(
            m_temporary->filePath(QStringLiteral("data/devhub.db")),
            m_temporary->filePath(QStringLiteral("backups"))));
        const auto opened = m_database->open();
        QVERIFY2(opened, opened ? "" : qPrintable(opened.error().technicalMessage));
        const auto migrated = m_database->migrate();
        QVERIFY2(migrated, migrated ? "" : qPrintable(migrated.error().technicalMessage));
        QCOMPARE(m_database->currentSchemaVersion(), 9);
        QSqlQuery query(m_database->database());
        QVERIFY(query.exec(
            QStringLiteral(
                "INSERT INTO accounts(id,username_normalized,display_username,password_hash) "
                "VALUES('%1','planner-user','Planner User','hash')")
                .arg(m_accountId)));
        QVERIFY(query.exec(
            QStringLiteral("INSERT INTO "
                           "workspaces(id,account_id,display_name,root_path,entered_path,canonical_"
                           "root_path) VALUES('%1','%2','Alpha','C:/Alpha','C:/Alpha','C:/Alpha')")
                .arg(m_workspaceA, m_accountId)));
        QVERIFY(query.exec(
            QStringLiteral("INSERT INTO "
                           "workspaces(id,account_id,display_name,root_path,entered_path,canonical_"
                           "root_path) VALUES('%1','%2','Beta','C:/Beta','C:/Beta','C:/Beta')")
                .arg(m_workspaceB, m_accountId)));
        auto account = domain::account::AccountId::fromString(m_accountId);
        QVERIFY(account);
        m_repository.reset(new infrastructure::planner::SqlPlannerRepository(
            *m_database, std::move(account).value()));
        m_service.reset(
            new application::planner::PlannerService(*m_repository, m_validation, m_ids, m_clock));
    }

    void cleanup()
    {
        m_service.reset();
        m_repository.reset();
        if (m_database)
        {
            const auto closed = m_database->close();
            QVERIFY2(closed, closed ? "" : qPrintable(closed.error().technicalMessage));
        }
        m_database.reset();
        m_temporary.reset();
    }

    void freshMigrationCreatesPlannerTablesAndIndexes()
    {
        QSqlQuery query(m_database->database());
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN "
            "('planner_tasks','planner_tags','planner_task_tags','planner_subtasks','planner_"
            "milestones','planner_task_dependencies','planner_import_records')")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 7);
        QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_key_check")));
        QVERIFY(!query.next());
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='index' "
                                          "AND name LIKE 'idx_planner_%'")));
        QVERIFY(query.next());
        QVERIFY(query.value(0).toInt() >= 8);
    }

    void taskCrudTagsSubtasksUnicodeAndPagination()
    {
        domain::planner::PlannerTask draft;
        draft.title = QStringLiteral("日本語の計画 🛰️");
        draft.description = QStringLiteral("Καλημέρα — مرحبًا — ") + QString(250000, QChar(0x03BB));
        draft.priority = QStringLiteral("high");
        draft.difficulty = QStringLiteral("hard");
        draft.dueDate = QDate(2026, 12, 31);
        draft.startDate = QDate(2026, 10, 1);
        draft.estimatedMinutes = 150;
        draft.actualTrackedMinutes = 23;
        draft.gitBranch = QStringLiteral("feature/planner");
        draft.githubIssueUrl = QStringLiteral("https://github.com/MisterKeke/DevHub/issues/12");
        draft.githubPullRequestUrl = QStringLiteral("https://github.com/MisterKeke/DevHub/pull/13");
        draft.notes = QStringLiteral("Keep this note");
        draft.blockedReason = QStringLiteral("Waiting for review");
        draft.tags = {QStringLiteral("created with task")};
        draft.subtasks.append({{}, {}, QStringLiteral("Created with the task"), false, 0});
        auto created = m_service->createTask(m_workspaceA, draft);
        QVERIFY2(created, created ? "" : qPrintable(created.error().technicalMessage));
        QCOMPARE(created.value().title, draft.title);
        QCOMPARE(created.value().description, draft.description);
        QCOMPARE(created.value().dueDate, draft.dueDate);
        QCOMPARE(created.value().gitBranch, draft.gitBranch);
        QCOMPARE(created.value().githubIssueUrl, draft.githubIssueUrl);
        QCOMPARE(created.value().githubPullRequestUrl, draft.githubPullRequestUrl);
        QCOMPARE(created.value().notes, draft.notes);
        QCOMPARE(created.value().blockedReason, draft.blockedReason);
        QCOMPARE(created.value().revision, qint64(1));
        QVERIFY(m_service->setTags(
            m_workspaceA, created.value().id,
            {QStringLiteral("Urgent"), QStringLiteral("urgent"), QStringLiteral("UI")}, 1));
        QCOMPARE(
            m_service
                ->addSubtask(m_workspaceA, created.value().id, QStringLiteral("第一項目"), 1, 2)
                .value()
                .position,
            1);
        QCOMPARE(
            m_service
                ->addSubtask(m_workspaceA, created.value().id, QStringLiteral("Deuxième"), 2, 3)
                .value()
                .position,
            2);
        auto read = m_service->getTask(m_workspaceA, created.value().id);
        QVERIFY(read);
        QCOMPARE(read.value().tags.size(), 2);
        QCOMPARE(read.value().subtasks.size(), 3);
        const QString initialSubtask = read.value().subtasks.at(0).id;
        QCOMPARE(read.value().subtasks.at(1).title, QStringLiteral("第一項目"));
        const QString firstSubtask = read.value().subtasks.at(1).id;
        const QString secondSubtask = read.value().subtasks.at(2).id;
        QVERIFY(m_service->reorderSubtasks(m_workspaceA, created.value().id,
                                           {secondSubtask, firstSubtask, initialSubtask}, 4));
        read = m_service->getTask(m_workspaceA, created.value().id);
        QVERIFY(read);
        QCOMPARE(read.value().subtasks.at(0).id, secondSubtask);
        auto editedSubtask = read.value().subtasks.at(0);
        editedSubtask.title = QStringLiteral("Updated second item");
        QVERIFY(m_service->updateSubtask(m_workspaceA, created.value().id, editedSubtask, 5));
        QVERIFY(m_service->removeSubtask(m_workspaceA, created.value().id, firstSubtask, 6));
        QVERIFY(!m_service->setTags(m_workspaceA, created.value().id, {}, 1));
        read = m_service->getTask(m_workspaceA, created.value().id);
        QVERIFY(read);
        QCOMPARE(read.value().subtasks.size(), 2);
        domain::planner::PlannerTask secondDraft;
        secondDraft.title = QStringLiteral("other");
        QVERIFY(m_service->createTask(m_workspaceA, secondDraft));
        domain::planner::PlannerTaskFilter filter;
        filter.searchText = QStringLiteral("計画");
        filter.limit = 1;
        auto page = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(page);
        QCOMPARE(page.value().size(), 1);
        filter.offset = 1;
        page = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(page);
        QVERIFY(page.value().isEmpty());
        filter.searchText.clear();
        filter.offset = 0;
        filter.tag = QStringLiteral("URGENT");
        page = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(page);
        QCOMPARE(page.value().size(), 1);
    }

    void statusRevisionMilestoneDependenciesAndWorkspaceIsolation()
    {
        domain::planner::PlannerTask first;
        first.title = QStringLiteral("First");
        domain::planner::PlannerTask second;
        second.title = QStringLiteral("Second");
        domain::planner::PlannerTask other;
        other.title = QStringLiteral("Other workspace");
        auto a = m_service->createTask(m_workspaceA, first);
        auto b = m_service->createTask(m_workspaceA, second);
        auto foreign = m_service->createTask(m_workspaceB, other);
        QVERIFY(a && b && foreign);
        QVERIFY(!m_service->getTask(m_workspaceB, a.value().id));
        auto impostorAccount = domain::account::AccountId::fromString(
            QStringLiteral("c3333333-3333-4333-8333-333333333333"));
        QVERIFY(impostorAccount);
        auto impostor = std::make_unique<infrastructure::planner::SqlPlannerRepository>(
            *m_database, std::move(impostorAccount).value());
        QVERIFY(!impostor->getTask(m_workspaceA, a.value().id));
        QVERIFY(m_service->addDependency(m_workspaceA, a.value().id, b.value().id, 1));
        QVERIFY(!m_service->addDependency(m_workspaceA, b.value().id, a.value().id, 1));
        QVERIFY(!m_service->addDependency(m_workspaceA, a.value().id, a.value().id, 2));
        QVERIFY(!m_service->addDependency(m_workspaceA, a.value().id, foreign.value().id, 2));
        auto inProgress = m_service->moveTask(m_workspaceA, a.value().id, 2,
                                              domain::planner::PlannerStatus::InProgress);
        QVERIFY(inProgress);
        QCOMPARE(inProgress.value().completed, false);
        QCOMPARE(inProgress.value().revision, qint64(3));
        QVERIFY(!m_service->moveTask(m_workspaceA, a.value().id, 2,
                                     domain::planner::PlannerStatus::Done));
        auto done = m_service->setCompleted(m_workspaceA, a.value().id, 3, true);
        QVERIFY(done);
        QCOMPARE(done.value().status, domain::planner::PlannerStatus::Done);
        QCOMPARE(done.value().completed, true);
        auto reopened = m_service->setCompleted(m_workspaceA, a.value().id, 4, false);
        QVERIFY(reopened);
        QCOMPARE(reopened.value().status, domain::planner::PlannerStatus::Todo);

        domain::planner::PlannerMilestone milestone;
        milestone.title = QStringLiteral("Release 1");
        milestone.dueDate = QDate(2027, 1, 10);
        auto createdMilestone = m_service->createMilestone(m_workspaceA, milestone);
        QVERIFY(createdMilestone);
        auto milestoneEdit = createdMilestone.value();
        milestoneEdit.title = QStringLiteral("Release 1.1");
        auto updatedMilestone = m_service->updateMilestone(milestoneEdit, 1);
        QVERIFY(updatedMilestone);
        QCOMPARE(updatedMilestone.value().revision, qint64(2));
        QVERIFY(!m_service->updateMilestone(milestoneEdit, 1));
        auto foreignWithMilestone = m_service->getTask(m_workspaceB, foreign.value().id);
        QVERIFY(foreignWithMilestone);
        foreignWithMilestone.value().milestoneId = createdMilestone.value().id;
        QVERIFY(!m_service->updateTask(foreignWithMilestone.value(), 1));
        auto edited = m_service->getTask(m_workspaceA, a.value().id);
        QVERIFY(edited);
        edited.value().milestoneId = createdMilestone.value().id;
        auto linked = m_service->updateTask(edited.value(), 5);
        QVERIFY(linked);
        QCOMPARE(linked.value().milestoneId, createdMilestone.value().id);
        auto milestones = m_service->listMilestones(m_workspaceA);
        QVERIFY(milestones);
        QCOMPARE(milestones.value().size(), 1);
        QVERIFY(m_service->removeMilestone(m_workspaceA, createdMilestone.value().id, 2));
        auto unlinked = m_service->getTask(m_workspaceA, a.value().id);
        QVERIFY(unlinked);
        QVERIFY(unlinked.value().milestoneId.isEmpty());
        QVERIFY(!m_service->getTask(m_workspaceA, foreign.value().id));
        QVERIFY(m_service->reorderTasks(m_workspaceA, {b.value().id, a.value().id}, {1, 7},
                                        domain::planner::PlannerStatus::Todo));
        domain::planner::PlannerTaskFilter board;
        board.status = domain::planner::PlannerStatus::Todo;
        board.sort = domain::planner::PlannerSortField::BoardPosition;
        auto ordered = m_service->listTasks(m_workspaceA, board);
        QVERIFY(ordered);
        QCOMPARE(ordered.value().size(), 2);
        QCOMPARE(ordered.value().at(0).id, b.value().id);
        auto dependencies = m_service->getTask(m_workspaceA, a.value().id);
        QVERIFY(dependencies);
        QCOMPARE(dependencies.value().dependencyIds, QStringList{b.value().id});
        QVERIFY(m_service->removeDependency(m_workspaceA, a.value().id, b.value().id, 8));
        dependencies = m_service->getTask(m_workspaceA, a.value().id);
        QVERIFY(dependencies);
        QVERIFY(dependencies.value().dependencyIds.isEmpty());
    }

    void importProvenanceKeepsWideIdentifiersAndArchiveRetainsData()
    {
        domain::planner::PlannerTask imported;
        imported.title = QStringLiteral("Imported task");
        imported.sourceType = QStringLiteral("somethingqt");
        imported.completed = true;
        imported.sourceProfileIdentity = QStringLiteral("profile-local-1");
        imported.sourceTaskId = QStringLiteral("9223372036854775807");
        imported.sourceRevision = QStringLiteral("18446744073709551615");
        imported.sourceCreatedAt = QStringLiteral("2020-01-02T03:04:05.123+02:00");
        imported.sourceUpdatedAt = QStringLiteral("2021-02-03T04:05:06.456Z");
        auto created = m_service->createTask(m_workspaceA, imported);
        QVERIFY2(created, created ? "" : qPrintable(created.error().technicalMessage));
        QCOMPARE(created.value().sourceTaskId, imported.sourceTaskId);
        QCOMPARE(created.value().sourceRevision, imported.sourceRevision);
        QCOMPARE(created.value().sourceCreatedAt, imported.sourceCreatedAt);
        QCOMPARE(created.value().status, domain::planner::PlannerStatus::Done);
        QVERIFY(!m_service->createTask(m_workspaceA, imported));
        QVERIFY(m_service->setTags(m_workspaceA, created.value().id, {QStringLiteral("persist me")},
                                   1));
        QSqlQuery archive(m_database->database());
        archive.prepare(
            QStringLiteral("UPDATE workspaces SET archived_at_utc='2026-10-22T00:00:00Z' WHERE "
                           "id=? AND account_id=?"));
        archive.addBindValue(m_workspaceA);
        archive.addBindValue(m_accountId);
        QVERIFY(archive.exec());
        auto archivedTask = m_service->getTask(m_workspaceA, created.value().id);
        QVERIFY(archivedTask);
        QCOMPARE(archivedTask.value().tags, QStringList{QStringLiteral("persist me")});
        archive.prepare(QStringLiteral(
            "UPDATE workspaces SET archived_at_utc=NULL WHERE id=? AND account_id=?"));
        archive.addBindValue(m_workspaceA);
        archive.addBindValue(m_accountId);
        QVERIFY(archive.exec());
        QCOMPARE(m_database->currentSchemaVersion(), 9);
    }

    void failedTagWriteRollsBackAndDatabaseCanReopen()
    {
        domain::planner::PlannerTask draft;
        draft.title = QStringLiteral("Rollback target");
        auto task = m_service->createTask(m_workspaceA, draft);
        QVERIFY(task);
        QVERIFY(m_service->setTags(m_workspaceA, task.value().id, {QStringLiteral("Persist")}, 1));
        QSqlQuery trigger(m_database->database());
        QVERIFY(trigger.exec(
            QStringLiteral("CREATE TRIGGER fail_planner_tag BEFORE INSERT ON planner_task_tags "
                           "WHEN (SELECT name FROM planner_tags WHERE id=NEW.tag_id)='Reject' "
                           "BEGIN SELECT RAISE(ABORT,'test rollback'); END")));
        auto changed =
            m_service->setTags(m_workspaceA, task.value().id,
                               {QStringLiteral("Keep temporarily"), QStringLiteral("Reject")}, 2);
        QVERIFY(!changed);
        auto after = m_service->getTask(m_workspaceA, task.value().id);
        QVERIFY(after);
        QCOMPARE(after.value().tags, QStringList{QStringLiteral("Persist")});
        QCOMPARE(after.value().revision, qint64(2));
        trigger.finish();
        trigger = QSqlQuery();
        const QString path = m_database->databaseFilePath();
        QVERIFY(m_database->close());
        m_repository.reset();
        m_service.reset();
        m_database.reset();
        m_database.reset(new infrastructure::database::InternalDatabaseManager(
            path, m_temporary->filePath(QStringLiteral("backups"))));
        QVERIFY(m_database->open());
        QVERIFY(m_database->migrate());
        auto account = domain::account::AccountId::fromString(m_accountId);
        QVERIFY(account);
        m_repository.reset(new infrastructure::planner::SqlPlannerRepository(
            *m_database, std::move(account).value()));
        m_service.reset(
            new application::planner::PlannerService(*m_repository, m_validation, m_ids, m_clock));
        auto persisted = m_service->getTask(m_workspaceA, task.value().id);
        QVERIFY(persisted);
        QCOMPARE(persisted.value().title, draft.title);
        QCOMPARE(persisted.value().tags, QStringList{QStringLiteral("Persist")});
    }

    void deletingTaskCascadesRelationsAndUsesOptimisticRevision()
    {
        domain::planner::PlannerTask draft;
        draft.title = QStringLiteral("Delete with children");
        auto task = m_service->createTask(m_workspaceA, draft);
        domain::planner::PlannerTask dependencyDraft;
        dependencyDraft.title = QStringLiteral("Dependency");
        auto dependency = m_service->createTask(m_workspaceA, dependencyDraft);
        QVERIFY(task && dependency);
        QVERIFY(m_service->setTags(m_workspaceA, task.value().id, {QStringLiteral("cascade")}, 1));
        QVERIFY(
            m_service->addSubtask(m_workspaceA, task.value().id, QStringLiteral("Child"), 0, 2));
        QVERIFY(m_service->addDependency(m_workspaceA, task.value().id, dependency.value().id, 3));
        QVERIFY(!m_service->removeTask(m_workspaceA, task.value().id, 3));
        QVERIFY(m_service->removeTask(m_workspaceA, task.value().id, 4));
        QVERIFY(!m_service->getTask(m_workspaceA, task.value().id));
        QSqlQuery query(m_database->database());
        for (const QString &table :
             {QStringLiteral("planner_task_tags"), QStringLiteral("planner_subtasks"),
              QStringLiteral("planner_task_dependencies")})
        {
            query.prepare(
                QStringLiteral("SELECT COUNT(*) FROM %1 WHERE workspace_id=? AND task_id=?")
                    .arg(table));
            query.addBindValue(m_workspaceA);
            query.addBindValue(task.value().id);
            QVERIFY(query.exec());
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 0);
            query.finish();
        }
    }

  private:
    const QString m_accountId = QStringLiteral("a1111111-1111-4111-8111-111111111111");
    const QString m_workspaceA = QStringLiteral("b1111111-1111-4111-8111-111111111111");
    const QString m_workspaceB = QStringLiteral("b2222222-2222-4222-8222-222222222222");
    std::unique_ptr<QTemporaryDir> m_temporary;
    std::unique_ptr<infrastructure::database::InternalDatabaseManager> m_database;
    std::unique_ptr<infrastructure::planner::SqlPlannerRepository> m_repository;
    application::planner::PlannerValidation m_validation;
    shared::identifiers::UuidIdGenerator m_ids;
    shared::time::SystemApplicationClock m_clock;
    std::unique_ptr<application::planner::PlannerService> m_service;
};
// NOLINTEND(readability-convert-member-functions-to-static,readability-function-cognitive-complexity)

QTEST_GUILESS_MAIN(PlannerTest)
#include "planner_test.moc"
