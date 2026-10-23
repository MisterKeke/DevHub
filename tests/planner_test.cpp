#include "../src/application/planner/PlannerService.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/infrastructure/planner/SqlPlannerRepository.h"
#include "../src/shared/identifiers/IdGenerator.h"
#include "../src/shared/time/ApplicationClock.h"
#include "../src/infrastructure/planner/SomethingQtImportReader.h"
#include "support/SyntheticSomethingProfile.h"

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
        QCOMPARE(m_database->currentSchemaVersion(), 12);
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
            "milestones','planner_task_dependencies','planner_import_records','planner_time_entries')")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 8);
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

    void timeEntriesMilestoneProgressAndDevelopmentFiltersSurviveRestart()
    {
        domain::planner::PlannerMilestone milestoneDraft;
        milestoneDraft.title = QStringLiteral("Phase 4 delivery");
        milestoneDraft.description = QStringLiteral("Developer planning metadata");
        milestoneDraft.dueDate = QDate::currentDate().addDays(14);
        const auto milestone = m_service->createMilestone(m_workspaceA, milestoneDraft);
        QVERIFY(milestone);

        domain::planner::PlannerTask prerequisiteDraft;
        prerequisiteDraft.title = QStringLiteral("Branch work");
        prerequisiteDraft.dueDate = QDate::currentDate().addDays(-1);
        prerequisiteDraft.gitBranch = QStringLiteral("feature/phase-four");
        prerequisiteDraft.milestoneId = milestone.value().id;
        prerequisiteDraft.notes = QStringLiteral("Planner-only notes");
        const auto prerequisite = m_service->createTask(m_workspaceA, prerequisiteDraft);
        QVERIFY(prerequisite);

        domain::planner::PlannerTask dependentDraft;
        dependentDraft.title = QStringLiteral("Imported dependent");
        dependentDraft.priority = QStringLiteral("high");
        dependentDraft.sourceType = QStringLiteral("somethingqt");
        dependentDraft.sourceProfileIdentity = QStringLiteral("source-profile");
        dependentDraft.sourceTaskId = QStringLiteral("9007199254740995");
        dependentDraft.milestoneId = milestone.value().id;
        const auto dependent = m_service->createTask(m_workspaceA, dependentDraft);
        QVERIFY(dependent);
        QVERIFY(m_service->addDependency(m_workspaceA, dependent.value().id,
                                         prerequisite.value().id, dependent.value().revision));

        auto blocked = m_service->getTask(m_workspaceA, dependent.value().id);
        QVERIFY(blocked);
        QVERIFY(blocked.value().blockedByDependency);
        QCOMPARE(blocked.value().incompletePrerequisiteTitles, QStringList{QStringLiteral("Branch work")});
        QCOMPARE(blocked.value().prerequisiteTitles, QStringList{QStringLiteral("Branch work")});
        QCOMPARE(m_service->getTask(m_workspaceA, prerequisite.value().id).value().dependentTaskTitles,
                 QStringList{QStringLiteral("Imported dependent")});

        auto addFirst = m_service->addTimeEntry(m_workspaceA, prerequisite.value().id, 35,
                                                QStringLiteral("Investigated branch"), 1);
        QVERIFY(addFirst);
        QVERIFY(m_service->addTimeEntry(m_workspaceA, prerequisite.value().id, 10,
                                        QStringLiteral("Fixed tests"), 2));
        auto timed = m_service->getTask(m_workspaceA, prerequisite.value().id);
        QVERIFY(timed);
        QCOMPARE(timed.value().actualTrackedMinutes, qint64(45));
        auto entries = m_service->listTimeEntries(m_workspaceA, prerequisite.value().id);
        QVERIFY(entries);
        QCOMPARE(entries.value().size(), 2);
        QString entry35Id;
        bool foundTen = false;
        for (const auto& entry : entries.value()) {
            if (entry.durationMinutes == 35) entry35Id = entry.id;
            if (entry.durationMinutes == 10) foundTen = true;
        }
        QVERIFY(!entry35Id.isEmpty());
        QVERIFY(foundTen);

        domain::planner::PlannerTaskFilter filter;
        filter.blocked = true;
        auto filtered = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(filtered);
        QCOMPARE(filtered.value().size(), 1);
        QCOMPARE(filtered.value().first().id, dependent.value().id);
        filter.blocked = false;
        filtered = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(filtered);
        QCOMPARE(filtered.value().size(), 1);
        QCOMPARE(filtered.value().first().id, prerequisite.value().id);
        filter.blocked.reset();
        filter.gitBranch = QStringLiteral("feature/phase-four");
        filtered = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(filtered);
        QCOMPARE(filtered.value().size(), 1);
        filter.gitBranch.clear();
        filter.milestoneId = milestone.value().id;
        filtered = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(filtered);
        QCOMPARE(filtered.value().size(), 2);
        filter.milestoneId.clear();
        filter.overdue = true;
        filtered = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(filtered);
        QCOMPARE(filtered.value().size(), 1);
        QCOMPARE(filtered.value().first().id, prerequisite.value().id);
        filter.overdue.reset();
        filter.sourceType = QStringLiteral("somethingqt");
        filtered = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(filtered);
        QCOMPARE(filtered.value().size(), 1);
        QCOMPARE(filtered.value().first().id, dependent.value().id);
        filter.sourceType.clear();
        filter.priority = QStringLiteral("high");
        filtered = m_service->listTasks(m_workspaceA, filter);
        QVERIFY(filtered);
        QCOMPARE(filtered.value().size(), 1);
        QCOMPARE(filtered.value().first().id, dependent.value().id);

        auto milestoneRows = m_service->listMilestones(m_workspaceA);
        QVERIFY(milestoneRows);
        QCOMPARE(milestoneRows.value().first().taskCount, 2);
        QCOMPARE(milestoneRows.value().first().completedTaskCount, 0);

        const QString databasePath = m_database->databaseFilePath();
        m_service.reset();
        m_repository.reset();
        QVERIFY(m_database->close());
        m_database.reset(new infrastructure::database::InternalDatabaseManager(
            databasePath, m_temporary->filePath(QStringLiteral("backups"))));
        QVERIFY(m_database->open());
        QVERIFY(m_database->migrate());
        auto account = domain::account::AccountId::fromString(m_accountId);
        QVERIFY(account);
        m_repository = std::make_unique<infrastructure::planner::SqlPlannerRepository>(
            *m_database, std::move(account).value());
        m_service = std::make_unique<application::planner::PlannerService>(
            *m_repository, m_validation, m_ids, m_clock);
        timed = m_service->getTask(m_workspaceA, prerequisite.value().id);
        QVERIFY(timed);
        QCOMPARE(timed.value().actualTrackedMinutes, qint64(45));
        QCOMPARE(m_service->listTimeEntries(m_workspaceA, prerequisite.value().id).value().size(), 2);

        auto done = m_service->setCompleted(m_workspaceA, prerequisite.value().id,
                                            timed.value().revision, true);
        QVERIFY(done);
        milestoneRows = m_service->listMilestones(m_workspaceA);
        QVERIFY(milestoneRows);
        QCOMPARE(milestoneRows.value().first().completedTaskCount, 1);
        blocked = m_service->getTask(m_workspaceA, dependent.value().id);
        QVERIFY(blocked);
        QVERIFY(!blocked.value().blockedByDependency);
        QVERIFY(m_service->removeTimeEntry(m_workspaceA, prerequisite.value().id,
                                           entry35Id, done.value().revision));
        timed = m_service->getTask(m_workspaceA, prerequisite.value().id);
        QVERIFY(timed);
        QCOMPARE(timed.value().actualTrackedMinutes, qint64(10));

        QVERIFY(m_validation.validateGitHubIssueUrl(QStringLiteral("https://github.com/org/repo/issues/42")));
        QVERIFY(!m_validation.validateGitHubIssueUrl(QStringLiteral("https://github.com/org/repo/pull/42")));
        QVERIFY(m_validation.validateGitHubPullRequestUrl(QStringLiteral("https://github.com/org/repo/pull/42")));
        QVERIFY(!m_validation.validateGitHubUrl(QStringLiteral("http://github.com/org/repo/issues/42")));
    }

    void linkedServiceActionsKeepBoundedWorkspaceScopedHistory()
    {
        domain::planner::PlannerTask draft;
        draft.title = QStringLiteral("Build the application");
        draft.linkedServiceId = QStringLiteral("11111111-1111-4111-8111-111111111111");
        auto task = m_service->createTask(m_workspaceA, draft);
        QVERIFY2(task, task ? "" : qPrintable(task.error().technicalMessage));
        QCOMPARE(task.value().linkedServiceId, draft.linkedServiceId);
        QVERIFY(!m_service->beginActionRun(m_workspaceB, task.value().id,
            QStringLiteral("service.start:%1").arg(draft.linkedServiceId)));
        QVERIFY(!m_service->beginActionRun(m_workspaceA, task.value().id, QStringLiteral("arbitrary.command")));

        auto first = m_service->beginActionRun(m_workspaceA, task.value().id,
            QStringLiteral("service.start:%1").arg(draft.linkedServiceId),
            QStringLiteral("service:%1:stdout").arg(draft.linkedServiceId));
        QVERIFY(first);
        QVERIFY(m_service->finishActionRun(m_workspaceA, task.value().id, first.value().id,
                                           false, QString(700, QLatin1Char('x'))));
        auto history = m_service->listActionRuns(m_workspaceA, task.value().id);
        QVERIFY(history);
        QCOMPARE(history.value().size(), 1);
        QCOMPARE(history.value().first().logReference, QStringLiteral("service:%1:stdout").arg(draft.linkedServiceId));
        QVERIFY(history.value().first().succeeded.has_value());
        QCOMPARE(*history.value().first().succeeded, false);
        QCOMPARE(history.value().first().errorSummary.size(), 512);
        QVERIFY(history.value().first().finishedAtUtc.isValid());

        for (int i = 0; i < 22; ++i) {
            auto run = m_service->beginActionRun(m_workspaceA, task.value().id,
                QStringLiteral("service.start:%1").arg(draft.linkedServiceId));
            QVERIFY2(run, run ? "" : qPrintable(run.error().technicalMessage));
            QVERIFY(m_service->finishActionRun(m_workspaceA, task.value().id, run.value().id,
                                               true));
        }
        history = m_service->listActionRuns(m_workspaceA, task.value().id, 1000);
        QVERIFY(history);
        QCOMPARE(history.value().size(), 20);
        QVERIFY(m_service->listActionRuns(m_workspaceB, task.value().id).value().isEmpty());
    }

    void importProvenanceKeepsWideIdentifiersAndArchiveRetainsData()
    {
        domain::planner::PlannerTask imported;
        imported.title = QStringLiteral("Imported task");
        imported.completed = true;
        imported.sourceProfileIdentity = QStringLiteral("profile-local-1");
        imported.sourceTaskId = QStringLiteral("9223372036854775807");
        imported.sourceRevision = QStringLiteral("18446744073709551615");
        imported.sourceCreatedAt = QStringLiteral("2020-01-02T03:04:05.123+02:00");
        imported.sourceUpdatedAt = QStringLiteral("2021-02-03T04:05:06.456Z");
        imported.description = QString(150000, QChar(0x03bb));
        imported.tags = {QStringLiteral("日本語"), QStringLiteral("🧪 test")};
        for (int position = 74; position >= 0; --position)
            imported.subtasks.append({{}, QString::number(8000 + position), QStringLiteral("Subtask %1").arg(position),
                                      position % 2 == 0, position});
        auto created = m_service->importTask(m_workspaceA, imported);
        QVERIFY2(created, created ? "" : qPrintable(created.error().technicalMessage));
        QCOMPARE(created.value().sourceTaskId, imported.sourceTaskId);
        QCOMPARE(created.value().sourceRevision, imported.sourceRevision);
        QCOMPARE(created.value().sourceCreatedAt, imported.sourceCreatedAt);
        QCOMPARE(created.value().status, domain::planner::PlannerStatus::Done);
        QCOMPARE(created.value().description, imported.description);
        QCOMPARE(created.value().subtasks.size(), 75);
        QCOMPARE(created.value().subtasks.first().position, 0);
        QCOMPARE(created.value().subtasks.first().sourceSubtaskId, QStringLiteral("8000"));
        auto counts = m_service->importedSourceTaskCounts(m_workspaceA, imported.sourceProfileIdentity);
        QVERIFY(counts);
        QCOMPARE(counts.value().value(imported.sourceTaskId), 1);
        QVERIFY(!m_service->importTask(m_workspaceA, imported));
        auto secondCopy = m_service->importTask(m_workspaceA, imported, true);
        QVERIFY(secondCopy);
        QVERIFY(secondCopy.value().id != created.value().id);
        auto otherWorkspace = m_service->importTask(m_workspaceB, imported);
        QVERIFY(otherWorkspace);
        QSqlQuery copyNumbers(m_database->database());
        copyNumbers.prepare(QStringLiteral("SELECT copy_number FROM planner_import_records WHERE workspace_id=? AND source_task_id=? ORDER BY copy_number"));
        copyNumbers.addBindValue(m_workspaceA); copyNumbers.addBindValue(imported.sourceTaskId);
        QVERIFY(copyNumbers.exec());
        QVERIFY(copyNumbers.next()); QCOMPARE(copyNumbers.value(0).toInt(), 1);
        QVERIFY(copyNumbers.next()); QCOMPARE(copyNumbers.value(0).toInt(), 2);
        QVERIFY(!copyNumbers.next());
        auto editedCopy = created.value();
        editedCopy.title = QStringLiteral("Edited independently");
        auto edited = m_service->updateTask(editedCopy, 1);
        QVERIFY(edited);
        QCOMPARE(edited.value().title, QStringLiteral("Edited independently"));
        QCOMPARE(edited.value().sourceRevision, imported.sourceRevision);
        QVERIFY(m_service->setTags(m_workspaceA, created.value().id, {QStringLiteral("persist me")},
                                   2));
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
        QVERIFY(!m_service->importTask(m_workspaceA, imported, true));
        archive.prepare(QStringLiteral(
            "UPDATE workspaces SET archived_at_utc=NULL WHERE id=? AND account_id=?"));
        archive.addBindValue(m_workspaceA);
        archive.addBindValue(m_accountId);
        QVERIFY(archive.exec());
        QCOMPARE(m_database->currentSchemaVersion(), 12);
    }

    void importedTaskFailureRollsBackTaskTagsSubtasksAndProvenance()
    {
        QSqlQuery trigger(m_database->database());
        QVERIFY(trigger.exec(QStringLiteral(
            "CREATE TRIGGER fail_import_subtask BEFORE INSERT ON planner_subtasks "
            "BEGIN SELECT RAISE(ABORT,'import child rejected'); END")));
        domain::planner::PlannerTask draft;
        draft.title = QStringLiteral("Atomic import");
        draft.sourceProfileIdentity = QStringLiteral("atomic-profile");
        draft.sourceTaskId = QStringLiteral("42");
        draft.sourceRevision = QStringLiteral("7");
        draft.tags = {QStringLiteral("atomic tag")};
        draft.subtasks.append({{}, QStringLiteral("8"), QStringLiteral("Fails"), false, 0});
        const auto result = m_service->importTask(m_workspaceA, draft);
        QVERIFY(!result);
        QSqlQuery query(m_database->database());
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM planner_tasks WHERE title='Atomic import'")));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM planner_tags WHERE name='atomic tag'")));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM planner_import_records WHERE source_task_id='42'")));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 0);
    }

    void importedCopySurvivesSourceDeletionAndCancellationStopsAtTaskBoundary()
    {
        tests::assistant::SyntheticProfile source;
        QVERIFY(source.created);
        const QString longDescription = QString(90000, QChar(0x03bb));
        QVERIFY(source.addTask(7001, QStringLiteral("Independent copy"), false, longDescription, 12,
                               QStringLiteral("2027-01-02")));
        QVERIFY(source.addTag(7001, QStringLiteral("persist")));
        QVERIFY(source.addSubtask(7001, QStringLiteral("Source child"), true, 4));
        const auto profile = infrastructure::planner::SomethingQtImportReader::inspect(source.path);
        QVERIFY2(profile.success, qPrintable(profile.message));
        const auto sourceTask = infrastructure::planner::SomethingQtImportReader::readTask(
            source.path, profile.value.identity, QStringLiteral("7001"));
        QVERIFY2(sourceTask.success, qPrintable(sourceTask.message));
        auto imported = m_service->importTask(m_workspaceA, sourceTask.value);
        QVERIFY2(imported, imported ? "" : qPrintable(imported.error().technicalMessage));
        QCOMPARE(imported.value().description, longDescription);
        QCOMPARE(imported.value().subtasks.size(), 1);
        QCOMPARE(imported.value().subtasks.first().sourceSubtaskId, sourceTask.value.subtasks.first().sourceSubtaskId);

        const QString sourceConnection = QStringLiteral("planner-import-delete-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto sourceDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), sourceConnection);
        sourceDb.setDatabaseName(source.path);
        QVERIFY(sourceDb.open());
        QSqlQuery remove(sourceDb);
        remove.prepare(QStringLiteral("DELETE FROM tasks WHERE id=?"));
        remove.addBindValue(7001);
        QVERIFY(remove.exec());
        remove = QSqlQuery(); sourceDb.close(); sourceDb = QSqlDatabase();
        QSqlDatabase::removeDatabase(sourceConnection);
        QVERIFY(!infrastructure::planner::SomethingQtImportReader::readTask(
            source.path, profile.value.identity, QStringLiteral("7001")).success);

        auto edited = imported.value();
        edited.title = QStringLiteral("Edited after source deletion");
        edited.description.append(QStringLiteral(" local"));
        edited.notes = QStringLiteral("Separate Planner developer note");
        const auto localEdit = m_service->updateTask(edited, 1);
        QVERIFY(localEdit);
        const auto persistedCopy = m_service->getTask(m_workspaceA, imported.value().id);
        QVERIFY(persistedCopy);
        QCOMPARE(persistedCopy.value().title, QStringLiteral("Edited after source deletion"));
        QCOMPARE(persistedCopy.value().description, longDescription + QStringLiteral(" local"));
        QCOMPARE(persistedCopy.value().notes, QStringLiteral("Separate Planner developer note"));

        QVERIFY(source.addTask(7002, QStringLiteral("Cancel boundary one")));
        QVERIFY(source.addTask(7003, QStringLiteral("Cancel boundary two")));
        const auto refreshedProfile = infrastructure::planner::SomethingQtImportReader::inspect(source.path);
        QVERIFY(refreshedProfile.success);
        infrastructure::planner::SomethingQtImportSelection all;
        all.all = true;
        application::tasks::CancellationSource cancellation;
        int importedBeforeCancel = 0;
        const auto stream = infrastructure::planner::SomethingQtImportReader::forEachTask(
            source.path, refreshedProfile.value.identity, all, cancellation.token(),
            [this, &importedBeforeCancel, &cancellation](domain::planner::PlannerTask task) {
                const auto saved = m_service->importTask(m_workspaceB, std::move(task));
                if (!saved) return false;
                ++importedBeforeCancel;
                cancellation.requestCancellation();
                return true;
            });
        QVERIFY(!stream.success);
        QVERIFY(stream.cancelled);
        QCOMPARE(importedBeforeCancel, 1);
        domain::planner::PlannerTaskFilter filter;
        auto localTasks = m_service->listTasks(m_workspaceB, filter);
        QVERIFY(localTasks);
        QCOMPARE(localTasks.value().size(), 1);
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
