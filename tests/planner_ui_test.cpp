#include "../src/application/planner/PlannerService.h"
#include "../src/application/planner/PlannerActivityService.h"
#include "../src/application/git/GitCommandRunner.h"
#include "../src/application/git/GitService.h"
#include "../src/application/settings/SettingsService.h"
#include "../src/application/tasks/TaskScheduler.h"
#include "../src/application/workspaces/WorkspaceContext.h"
#include "../src/domain/errors/AppError.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/infrastructure/planner/SqlPlannerRepository.h"
#include "../src/infrastructure/planner/SqlPlannerActivityRepository.h"
#include "../src/presentation/overview/planner/PlannerController.h"
#include "../src/shared/identifiers/IdGenerator.h"
#include "../src/shared/time/ApplicationClock.h"

#include <QDir>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QtTest/QtTest>
#include <algorithm>

using namespace devhub;

class PlannerUiSettings final : public application::settings::ISettingsRepository
{
public:
    shared::common::Result<application::settings::SettingValues> load(
        application::settings::SettingScope scope, const QString& workspaceId = {}) override
    {
        return shared::common::Result<application::settings::SettingValues>::success(
            m_values.value(key(scope, workspaceId)));
    }
    shared::common::Result<void> save(application::settings::SettingScope scope,
        const QString& workspaceId, const QString& id, const QVariant& value) override
    {
        m_values[key(scope, workspaceId)].insert(id, value);
        return shared::common::Result<void>::success();
    }
    shared::common::Result<void> saveBatch(const application::settings::SettingUpdates& updates) override
    {
        for (const auto& update : updates) {
            auto result = save(update.scope, update.workspaceId, update.id, update.value);
            if (!result) return result;
        }
        return shared::common::Result<void>::success();
    }
private:
    static QString key(application::settings::SettingScope scope, const QString& workspaceId)
    { return QString::number(static_cast<int>(scope)) + QLatin1Char(':') + workspaceId; }
    QHash<QString, application::settings::SettingValues> m_values;
};

class PlannerUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        m_temp.reset(new QTemporaryDir(QDir::current().filePath(QStringLiteral("planner-ui-XXXXXX"))));
        QVERIFY(m_temp->isValid());
        m_database.reset(new infrastructure::database::InternalDatabaseManager(
            m_temp->filePath(QStringLiteral("data/devhub.db")), m_temp->filePath(QStringLiteral("backups"))));
        auto opened = m_database->open(); QVERIFY2(opened, qPrintable(opened.error().technicalMessage));
        auto migrated = m_database->migrate(); QVERIFY2(migrated, qPrintable(migrated.error().technicalMessage));
        QSqlQuery query(m_database->database());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO accounts(id,username_normalized,display_username,password_hash) VALUES('%1','planner-ui','Planner UI','hash')").arg(m_accountId)));
        for (const auto& entry : {qMakePair(m_workspaceA, QStringLiteral("Alpha")), qMakePair(m_workspaceB, QStringLiteral("Beta"))})
            QVERIFY(query.exec(QStringLiteral("INSERT INTO workspaces(id,account_id,display_name,root_path,entered_path,canonical_root_path) VALUES('%1','%2','%3','C:/%3','C:/%3','C:/%3')").arg(entry.first, m_accountId, entry.second)));
        auto account = domain::account::AccountId::fromString(m_accountId); QVERIFY(account);
        m_account = std::move(account).value();
        createServices();
        m_settingsRepository = std::make_unique<PlannerUiSettings>();
        m_settingsService = std::make_unique<application::settings::SettingsService>(*m_settingsRepository);
        m_context = std::make_unique<application::workspaces::WorkspaceContext>();
        m_context->activate(makeWorkspace(m_workspaceA, QStringLiteral("Alpha")));
        m_controller = std::make_unique<presentation::overview::planner::PlannerController>(
            *m_service, *m_settingsService, *m_context, nullptr, nullptr, nullptr, m_activityService.get());
    }

    void cleanup()
    {
        m_controller.reset(); m_context.reset(); m_settingsService.reset(); m_settingsRepository.reset();
        m_service.reset(); m_repository.reset(); m_activityService.reset(); m_activityRepository.reset();
        if (m_database) { const auto result = m_database->close(); QVERIFY2(result, qPrintable(result.error().technicalMessage)); }
        m_database.reset(); m_temp.reset();
    }

    void panelControllerCreatesEditsFiltersAndSelectsCalendarDates()
    {
        using namespace presentation::overview::planner;
        QVERIFY(m_controller->hasWorkspace());
        QCOMPARE(m_controller->workspaceName(), QStringLiteral("Alpha"));
        QCOMPARE(m_controller->totalCount(), 0);
        QVariantMap draft{{QStringLiteral("title"), QStringLiteral("Ship Planner UI")},
            {QStringLiteral("description"), QStringLiteral("Embedded on Overview")},
            {QStringLiteral("priority"), QStringLiteral("high")}, {QStringLiteral("difficulty"), QStringLiteral("hard")},
            {QStringLiteral("status"), QStringLiteral("todo")}, {QStringLiteral("completed"), false},
            {QStringLiteral("dueDate"), QDate::currentDate().addDays(4).toString(Qt::ISODate)},
            {QStringLiteral("startDate"), QDate::currentDate().toString(Qt::ISODate)},
            {QStringLiteral("estimatedMinutes"), 90}, {QStringLiteral("actualTrackedMinutes"), 12},
            {QStringLiteral("tags"), QStringLiteral("ui, phase2")},
            {QStringLiteral("subtasks"), QVariantList{QVariantMap{{QStringLiteral("title"), QStringLiteral("Build the panel")}, {QStringLiteral("completed"), false}}}}};
        QVERIFY2(m_controller->saveTask(draft), qPrintable(m_controller->error()));
        QCOMPARE(m_controller->totalCount(), 1);
        QCOMPARE(m_controller->statusCount(QStringLiteral("todo")), 1);
        QCOMPARE(m_controller->listModel()->rowCount(), 1);
        const auto taskIndex = m_controller->tasks()->index(0, 0);
        const QString taskId = taskIndex.data(PlannerTaskModel::IdRole).toString();
        QVERIFY(!taskId.isEmpty());
        QCOMPARE(taskIndex.data(PlannerTaskModel::TitleRole).toString(), QStringLiteral("Ship Planner UI"));
        const auto modelSubtasks = taskIndex.data(PlannerTaskModel::SubtasksRole).toList();
        QCOMPARE(modelSubtasks.size(), 1);
        QCOMPARE(modelSubtasks.first().toMap().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Build the panel"));
        const QString dueDate = QDate::currentDate().addDays(4).toString(Qt::ISODate);
        QCOMPARE(m_controller->countForDate(dueDate), 1);
        m_controller->setSelectedDate(dueDate);
        QCOMPARE(m_controller->calendarModel()->rowCount(), 1);
        m_controller->setSearchText(QStringLiteral("missing"));
        QCOMPARE(m_controller->listModel()->rowCount(), 0);
        QCOMPARE(m_controller->countForDate(dueDate), 0);
        m_controller->setSearchText(QString{});
        m_controller->setTagFilter(QStringLiteral("phase2"));
        QCOMPARE(m_controller->listModel()->rowCount(), 1);
        m_controller->setPriorityFilter(QStringLiteral("low"));
        QCOMPARE(m_controller->listModel()->rowCount(), 0);
        m_controller->setPriorityFilter(QString{});
        m_controller->setTagFilter(QString{});

        auto edited = m_controller->taskDetails(taskId);
        edited.insert(QStringLiteral("title"), QStringLiteral("Ship the completed Planner UI"));
        edited.insert(QStringLiteral("priority"), QStringLiteral("medium"));
        edited.insert(QStringLiteral("tags"), QStringLiteral("ui, shipped"));
        edited.insert(QStringLiteral("notes"), QStringLiteral("Saved through PlannerController"));
        auto subs = edited.value(QStringLiteral("subtasks")).toList();
        auto subtask = subs.first().toMap();
        subtask.insert(QStringLiteral("completed"), true); subs[0] = subtask;
        edited.insert(QStringLiteral("subtasks"), subs);
        QVERIFY2(m_controller->saveTask(edited), qPrintable(m_controller->error()));
        auto current = m_controller->taskDetails(taskId);
        QCOMPARE(current.value(QStringLiteral("title")).toString(), QStringLiteral("Ship the completed Planner UI"));
        QCOMPARE(current.value(QStringLiteral("notes")).toString(), QStringLiteral("Saved through PlannerController"));
        QCOMPARE(current.value(QStringLiteral("completedSubtaskCount")).toInt(), 1);
        QCOMPARE(current.value(QStringLiteral("tags")).toStringList(), QStringList({QStringLiteral("shipped"), QStringLiteral("ui")}));

        QVERIFY(m_controller->moveTask(taskId, QStringLiteral("in_progress")));
        QCOMPARE(m_controller->inProgressCount(), 1);
        QVERIFY(m_controller->setTaskCompleted(taskId, true));
        QCOMPARE(m_controller->completedCount(), 1);
        QCOMPARE(m_controller->progressPercent(), 100);
        current = m_controller->taskDetails(taskId);
        QCOMPARE(current.value(QStringLiteral("status")).toString(), QStringLiteral("done"));
        QVERIFY(m_controller->setTaskCompleted(taskId, false));
        QCOMPARE(m_controller->completedCount(), 0);
        QCOMPARE(m_controller->taskDetails(taskId).value(QStringLiteral("status")).toString(), QStringLiteral("todo"));
        QVERIFY(m_controller->saveTask({{QStringLiteral("title"), QStringLiteral("Second task")}}));
        QVERIFY(m_controller->saveTask({{QStringLiteral("title"), QStringLiteral("Third task")}}));
        const QString secondId = m_controller->tasks()->index(1, 0).data(PlannerTaskModel::IdRole).toString();
        QVERIFY(m_controller->shiftTaskOrder(secondId, -1));
        QCOMPARE(m_controller->tasks()->index(0, 0).data(PlannerTaskModel::IdRole).toString(), secondId);
    }

    void workspaceSwitchSettingsAndSessionBoundary()
    {
        QVariantMap draft{{QStringLiteral("title"), QStringLiteral("Alpha only")}};
        QVERIFY(m_controller->saveTask(draft));
        m_controller->setView(QStringLiteral("calendar"));
        m_controller->setCollapsed(true);
        QCOMPARE(m_controller->view(), QStringLiteral("calendar"));
        QVERIFY(m_controller->collapsed());
        m_context->activate(makeWorkspace(m_workspaceB, QStringLiteral("Beta")));
        QCOMPARE(m_controller->workspaceId(), m_workspaceB);
        QCOMPARE(m_controller->workspaceName(), QStringLiteral("Beta"));
        QCOMPARE(m_controller->totalCount(), 0);
        QVERIFY(!m_controller->collapsed());
        QCOMPARE(m_controller->view(), QStringLiteral("board"));
        QVERIFY(m_controller->saveTask({{QStringLiteral("title"), QStringLiteral("Beta only")}}));
        QCOMPARE(m_controller->totalCount(), 1);
        m_context->activate(makeWorkspace(m_workspaceA, QStringLiteral("Alpha")));
        QCOMPARE(m_controller->totalCount(), 1);
        QCOMPARE(m_controller->taskDetails(m_controller->tasks()->index(0,0).data(presentation::overview::planner::PlannerTaskModel::IdRole).toString()).value(QStringLiteral("title")).toString(), QStringLiteral("Alpha only"));
        QCOMPARE(m_controller->view(), QStringLiteral("calendar"));
        QVERIFY(m_controller->collapsed());
        m_controller->setSessionAllowed(false);
        QCOMPARE(m_controller->totalCount(), 0);
        QVERIFY(!m_controller->hasWorkspace());
        m_controller->setSessionAllowed(true);
        QCOMPARE(m_controller->totalCount(), 1);
    }

    void deletedBranchAssociationIsVisibleAndSafeToRetain()
    {
        domain::planner::PlannerTask draft;
        draft.title = QStringLiteral("Task on removed branch");
        draft.gitBranch = QStringLiteral("feature/deleted-branch");
        const auto created = m_service->createTask(m_workspaceA, draft);
        QVERIFY(created);
        m_controller->refresh();
        const auto detail = m_controller->taskDetails(created.value().id);
        QCOMPARE(detail.value(QStringLiteral("gitBranch")).toString(), QStringLiteral("feature/deleted-branch"));
        QVERIFY(!detail.value(QStringLiteral("gitBranchExists")).toBool());
        const auto options = m_controller->branchOptionsForTask(created.value().id);
        bool foundMissing = false;
        for (const auto& value : options) {
            const auto option = value.toMap();
            if (option.value(QStringLiteral("name")).toString() == QStringLiteral("feature/deleted-branch")) {
                foundMissing = true;
                QVERIFY(!option.value(QStringLiteral("exists")).toBool());
                QVERIFY(option.value(QStringLiteral("displayName")).toString().contains(QStringLiteral("missing")));
            }
        }
        QVERIFY(foundMissing);
        auto edited = detail;
        edited.insert(QStringLiteral("notes"), QStringLiteral("Keep metadata after branch deletion"));
        QVERIFY2(m_controller->saveTask(edited), qPrintable(m_controller->error()));
        QCOMPARE(m_controller->taskDetails(created.value().id).value(QStringLiteral("gitBranch")).toString(),
                 QStringLiteral("feature/deleted-branch"));
        QCOMPARE(m_controller->taskDetails(created.value().id).value(QStringLiteral("notes")).toString(),
                 QStringLiteral("Keep metadata after branch deletion"));
    }

    void activityTimelineFiltersPlannerAndPullRequestEvents()
    {
        QVERIFY(m_controller->saveTask({{QStringLiteral("title"), QStringLiteral("Timeline task")}}));
        const QString taskId = m_controller->tasks()->index(0, 0).data(
            presentation::overview::planner::PlannerTaskModel::IdRole).toString();
        auto detail = m_controller->taskDetails(taskId);
        detail.insert(QStringLiteral("githubPullRequestUrl"), QStringLiteral("https://github.com/example/app/pull/8"));
        QVERIFY2(m_controller->saveTask(detail), qPrintable(m_controller->error()));
        QVERIFY(m_controller->moveTask(taskId, QStringLiteral("in_progress")));

        const auto all = m_controller->activityEvents(taskId, QStringLiteral("all"));
        const auto taskEvents = m_controller->activityEvents(taskId, QStringLiteral("task"));
        const auto gitEvents = m_controller->activityEvents(taskId, QStringLiteral("git"));
        QVERIFY(std::any_of(all.cbegin(), all.cend(), [](const QVariant& event) {
            return event.toMap().value(QStringLiteral("eventType")).toString() == QStringLiteral("task.created");
        }));
        QVERIFY(std::any_of(taskEvents.cbegin(), taskEvents.cend(), [](const QVariant& event) {
            return event.toMap().value(QStringLiteral("eventType")).toString() == QStringLiteral("task.status");
        }));
        QVERIFY(std::any_of(gitEvents.cbegin(), gitEvents.cend(), [](const QVariant& event) {
            return event.toMap().value(QStringLiteral("eventType")).toString() == QStringLiteral("pr.reference");
        }));
        QVERIFY(m_controller->activityEvents(taskId, QStringLiteral("services")).isEmpty());
        QVERIFY(m_controller->activityEvents(taskId, QStringLiteral("builds")).isEmpty());
    }

    void linkedBranchHistoryExcludesBaseAndReadsTaskReferencesFromCommitBodies()
    {
        const QString gitExecutable = QStandardPaths::findExecutable(QStringLiteral("git"));
        if (gitExecutable.isEmpty()) QSKIP("Git is required for the temporary branch history fixture.");
        QTemporaryDir repositoryDir(QDir::current().filePath(QStringLiteral("planner-git-XXXXXX")));
        QVERIFY(repositoryDir.isValid());
        auto runGit = [&repositoryDir, &gitExecutable](QStringList arguments, QByteArray* output = nullptr) {
            QProcess process;
            process.setWorkingDirectory(repositoryDir.path());
            process.start(gitExecutable, arguments);
            if (!process.waitForStarted(5000) || !process.waitForFinished(15000) || process.exitCode() != 0)
                return false;
            if (output) *output = process.readAllStandardOutput();
            return true;
        };
        QVERIFY(runGit({QStringLiteral("init"), QStringLiteral("--initial-branch=main")}));
        QVERIFY(runGit({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Planner Test")}));
        QVERIFY(runGit({QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("planner@example.invalid")}));
        QFile baseFile(repositoryDir.filePath(QStringLiteral("base.txt")));
        QVERIFY(baseFile.open(QIODevice::WriteOnly)); baseFile.write("base\n"); baseFile.close();
        QVERIFY(runGit({QStringLiteral("add"), QStringLiteral("base.txt")}));
        QVERIFY(runGit({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("Base commit")}));

        domain::planner::PlannerTask draft; draft.title = QStringLiteral("Task with branch commits");
        draft.gitBranch = QStringLiteral("feature/activity");
        auto task = m_service->createTask(m_workspaceA, draft); QVERIFY(task);
        const QString reference = application::planner::PlannerActivityService::taskReference(task.value().id);
        QVERIFY(runGit({QStringLiteral("switch"), QStringLiteral("-c"), QStringLiteral("feature/activity")}));
        QFile featureFile(repositoryDir.filePath(QStringLiteral("feature.txt")));
        QVERIFY(featureFile.open(QIODevice::WriteOnly)); featureFile.write("feature\n"); featureFile.close();
        QVERIFY(runGit({QStringLiteral("add"), QStringLiteral("feature.txt")}));
        QVERIFY(runGit({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("Implement authentication"),
                        QStringLiteral("-m"), QStringLiteral("Follow-up for %1").arg(reference)}));

        application::git::GitService git{application::git::GitCommandRunner(gitExecutable)};
        const auto commits = git.branchHistory(repositoryDir.path(), QStringLiteral("feature/activity"),
                                                QStringLiteral("main"), 100);
        QVERIFY2(commits, commits ? "" : qPrintable(commits.error().technicalMessage));
        QCOMPARE(commits.value().size(), 1);
        QCOMPARE(commits.value().first().subject, QStringLiteral("Implement authentication"));
        QVERIFY(commits.value().first().authoredAt.isValid());
        QVERIFY(commits.value().first().committedAt.isValid());
        QVERIFY(application::planner::PlannerActivityService::messageReferencesTask(
            commits.value().first().message, task.value().id));
        auto reachable = git.commitReachableFromAnyRef(repositoryDir.path(), commits.value().first().objectId);
        QVERIFY2(reachable, reachable ? "" : qPrintable(reachable.error().technicalMessage));
        QVERIFY(reachable.value());

        const auto localHistory = git.history(repositoryDir.path(), 0, 100);
        QVERIFY(localHistory);
        const auto base = std::find_if(localHistory.value().cbegin(), localHistory.value().cend(), [](const auto& item) {
            return item.subject == QStringLiteral("Base commit");
        });
        QVERIFY(base != localHistory.value().cend());
        const QString baseSha = base->objectId;

        const domain::workspace::Workspace repositoryWorkspace(
            domain::workspace::WorkspaceId::fromString(m_workspaceA).value(), m_account, QStringLiteral("Alpha"),
            domain::workspace::WorkspacePath(repositoryDir.path(), repositoryDir.path()),
            domain::workspace::WorkspaceStatus::Ready, false, 0, QDateTime::currentDateTimeUtc());
        application::workspaces::WorkspaceContext repositoryContext;
        repositoryContext.activate(repositoryWorkspace);
        application::tasks::TaskScheduler scheduler(m_ids);
        presentation::overview::planner::PlannerController scanController(
            *m_service, *m_settingsService, repositoryContext, nullptr, &scheduler, nullptr, m_activityService.get());
        scanController.refreshGitActivity(task.value().id);
        QVERIFY(scanController.gitActivityStatus().contains(QStringLiteral("Scanning")));
        QTRY_VERIFY_WITH_TIMEOUT(scanController.gitActivityStatus().contains(QStringLiteral("Reviewed")), 20000);
        auto links = m_activityService->listGitCommits(m_workspaceA, task.value().id);
        QVERIFY(links); QCOMPARE(links.value().size(), 1);
        QCOMPARE(links.value().first().commitSha, commits.value().first().objectId);
        QCOMPARE(links.value().first().associationSource, QStringLiteral("message"));
        QCOMPARE(links.value().first().branchName, QStringLiteral("feature/activity"));
        scanController.refreshGitActivity(task.value().id, true);
        QVERIFY(scanController.gitActivityStatus().contains(QStringLiteral("Scanning")));
        QTRY_VERIFY_WITH_TIMEOUT(scanController.gitActivityStatus().contains(QStringLiteral("Reviewed")), 20000);
        auto suggestions = scanController.gitCommitSuggestions();
        QVERIFY(std::any_of(suggestions.cbegin(), suggestions.cend(), [&baseSha](const QVariant& row) {
            return row.toMap().value(QStringLiteral("sha")).toString() == baseSha;
        }));
        QVERIFY(scanController.linkSuggestedGitCommits(task.value().id, {baseSha}));

        QFile externalFile(repositoryDir.filePath(QStringLiteral("external.txt")));
        QVERIFY(externalFile.open(QIODevice::WriteOnly)); externalFile.write("external\n"); externalFile.close();
        QVERIFY(runGit({QStringLiteral("add"), QStringLiteral("external.txt")}));
        QVERIFY(runGit({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("External follow-up")}));
        QFile secondExternalFile(repositoryDir.filePath(QStringLiteral("external-second.txt")));
        QVERIFY(secondExternalFile.open(QIODevice::WriteOnly)); secondExternalFile.write("external 2\n"); secondExternalFile.close();
        QVERIFY(runGit({QStringLiteral("add"), QStringLiteral("external-second.txt")}));
        QVERIFY(runGit({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("Second external follow-up")}));
        scanController.refreshGitActivity(task.value().id);
        QVERIFY(scanController.gitActivityStatus().contains(QStringLiteral("Scanning")));
        QTRY_VERIFY_WITH_TIMEOUT(scanController.gitActivityStatus().contains(QStringLiteral("Reviewed")), 20000);
        suggestions = scanController.gitCommitSuggestions();
        QVariantList externalShas;
        for (const QVariant& row : suggestions) {
            const QString subject = row.toMap().value(QStringLiteral("subject")).toString();
            if (subject == QStringLiteral("External follow-up")
                || subject == QStringLiteral("Second external follow-up"))
                externalShas.append(row.toMap().value(QStringLiteral("sha")));
        }
        QCOMPARE(externalShas.size(), 2);
        const QString externalSha = externalShas.first().toString();
        const QString secondExternalSha = externalShas.last().toString();
        QVERIFY(scanController.linkSuggestedGitCommits(task.value().id, externalShas));

        QVERIFY(runGit({QStringLiteral("switch"), QStringLiteral("main")}));
        QVERIFY(runGit({QStringLiteral("branch"), QStringLiteral("-D"), QStringLiteral("feature/activity")}));
        reachable = git.commitReachableFromAnyRef(repositoryDir.path(), commits.value().first().objectId);
        QVERIFY2(reachable, reachable ? "" : qPrintable(reachable.error().technicalMessage));
        QVERIFY(!reachable.value());
        scanController.refreshGitActivity(task.value().id);
        QVERIFY(scanController.gitActivityStatus().contains(QStringLiteral("Scanning")));
        QTRY_VERIFY_WITH_TIMEOUT(scanController.gitActivityStatus().contains(QStringLiteral("branch is missing")), 20000);
        links = m_activityService->listGitCommits(m_workspaceA, task.value().id);
        QVERIFY(links); QCOMPARE(links.value().size(), 4);
        const auto bySha = [&links](const QString& sha) {
            const auto found = std::find_if(links.value().cbegin(), links.value().cend(), [&sha](const auto& link) {
                return link.commitSha == sha;
            });
            return found == links.value().cend() ? QString{} : found->availability;
        };
        QCOMPARE(bySha(baseSha), QStringLiteral("reachable"));
        QCOMPARE(bySha(commits.value().first().objectId), QStringLiteral("unavailable"));
        QCOMPARE(bySha(externalSha), QStringLiteral("unavailable"));
        QCOMPARE(bySha(secondExternalSha), QStringLiteral("unavailable"));
        QTRY_VERIFY_WITH_TIMEOUT(scheduler.activeTaskCount() == 0, 10000);
    }

    void milestoneTimeAndMetadataFiltersUseControllerCommands()
    {
        using namespace presentation::overview::planner;
        QVERIFY(m_controller->saveMilestone({{QStringLiteral("title"), QStringLiteral("Controller milestone")},
            {QStringLiteral("description"), QStringLiteral("Workspace scoped")},
            {QStringLiteral("dueDate"), QDate::currentDate().addDays(10).toString(Qt::ISODate)}}));
        QCOMPARE(m_controller->milestones().size(), 1);
        const QString milestoneId = m_controller->milestones().first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(m_controller->saveTask({{QStringLiteral("title"), QStringLiteral("Controller task")},
            {QStringLiteral("priority"), QStringLiteral("high")},
            {QStringLiteral("dueDate"), QDate::currentDate().addDays(-1).toString(Qt::ISODate)},
            {QStringLiteral("milestoneId"), milestoneId}, {QStringLiteral("gitBranch"), QString{}}}));
        const QString taskId = m_controller->tasks()->index(0, 0).data(PlannerTaskModel::IdRole).toString();
        QVERIFY(m_controller->addTimeEntry(taskId, QStringLiteral("25"), QStringLiteral("Implementation")));
        auto detail = m_controller->taskDetails(taskId);
        QCOMPARE(detail.value(QStringLiteral("actualTrackedMinutes")).toLongLong(), qint64(25));
        const auto entries = m_controller->timeEntries(taskId);
        QCOMPARE(entries.size(), 1);
        QVERIFY(m_controller->removeTimeEntry(taskId, entries.first().toMap().value(QStringLiteral("id")).toString()));
        QCOMPARE(m_controller->taskDetails(taskId).value(QStringLiteral("actualTrackedMinutes")).toLongLong(), qint64(0));

        m_controller->setMilestoneFilter(milestoneId);
        QTRY_COMPARE(m_controller->totalCount(), 1);
        m_controller->setMilestoneFilter(QString{});
        m_controller->setGitBranchFilter(QStringLiteral("feature/no-match"));
        QTRY_COMPARE(m_controller->totalCount(), 0);
        m_controller->setGitBranchFilter(QString{});
        m_controller->setBlockedFilter(1);
        QTRY_COMPARE(m_controller->totalCount(), 0);
        QVERIFY(m_controller->taskDetails(taskId).contains(QStringLiteral("title")));
        QVERIFY(m_controller->setTaskCompleted(taskId, true));
        QVERIFY(m_controller->setTaskCompleted(taskId, false));
        m_controller->setBlockedFilter(-1);
        m_controller->setOverdueFilter(1);
        QTRY_COMPARE(m_controller->totalCount(), 1);
        m_controller->setOverdueFilter(-1);
        m_controller->setOriginFilter(QStringLiteral("local"));
        QTRY_COMPARE(m_controller->totalCount(), 1);
        m_controller->setPriorityFilter(QStringLiteral("low"));
        QTRY_COMPARE(m_controller->totalCount(), 0);
        m_controller->setPriorityFilter(QString{});
        QTRY_COMPARE(m_controller->totalCount(), 1);

        QVERIFY(m_controller->setTaskCompleted(taskId, true));
        QCOMPARE(m_controller->milestones().first().toMap().value(QStringLiteral("completedTaskCount")).toInt(), 1);
        QVERIFY(m_controller->saveMilestone({{QStringLiteral("id"), milestoneId},
            {QStringLiteral("title"), QStringLiteral("Edited milestone")},
            {QStringLiteral("description"), QStringLiteral("Edited details")},
            {QStringLiteral("completed"), true}}));
        QCOMPARE(m_controller->milestones().first().toMap().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Edited milestone"));
        QVERIFY(m_controller->removeMilestone(milestoneId));
        QVERIFY(m_controller->taskDetails(taskId).value(QStringLiteral("milestoneId")).toString().isEmpty());
    }

    void linkedServiceStartRequiresConfiguredServiceAndCurrentWorkspace()
    {
        const QString serviceId = QStringLiteral("11111111-1111-4111-8111-111111111111");
        m_controller->setServiceOptions({QVariantMap{{QStringLiteral("id"), serviceId},
                                                     {QStringLiteral("name"), QStringLiteral("Dev server")}}});
        QVERIFY(m_controller->saveTask({{QStringLiteral("title"), QStringLiteral("Task linked to saved service")},
                                        {QStringLiteral("linkedServiceId"), serviceId}}));
        const QString taskId = m_controller->tasks()->index(0, 0)
            .data(presentation::overview::planner::PlannerTaskModel::IdRole).toString();
        QVERIFY(!taskId.isEmpty());
        QCOMPARE(m_controller->taskDetails(taskId).value(QStringLiteral("linkedServiceId")).toString(), serviceId);
        QSignalSpy startSpy(m_controller.get(), &presentation::overview::planner::PlannerController::startLinkedServiceRequested);
        m_controller->startLinkedService(taskId);
        QCOMPARE(startSpy.size(), 1);
        QCOMPARE(startSpy.first().at(0).toString(), m_workspaceA);
        QCOMPARE(startSpy.first().at(2).toString(), serviceId);

        QSignalSpy cancelSpy(m_controller.get(), &presentation::overview::planner::PlannerController::cancelLinkedServiceStartRequested);
        m_controller->cancelLinkedServiceStart(taskId);
        QCOMPARE(cancelSpy.size(), 1);
        QCOMPARE(cancelSpy.first().at(1).toString(), taskId);
        const auto run = m_controller->beginLinkedServiceAction(taskId, serviceId,
            QStringLiteral("service:%1:stdout").arg(serviceId));
        QVERIFY(run.has_value());
        QVERIFY(m_controller->finishActionRun(*run, false, QStringLiteral("Synthetic launch failure")));
        const QVariantList history = m_controller->actionRuns(taskId);
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().toMap().value(QStringLiteral("status")).toString(), QStringLiteral("Failed"));
        QCOMPARE(history.first().toMap().value(QStringLiteral("logReference")).toString(),
                 QStringLiteral("service:%1:stdout").arg(serviceId));

        m_controller->setSessionAllowed(false);
        m_controller->startLinkedService(taskId);
        QVERIFY(!m_controller->beginLinkedServiceAction(taskId, serviceId, {}));
        QCOMPARE(startSpy.size(), 1);
        m_controller->setSessionAllowed(true);

        m_context->activate(makeWorkspace(m_workspaceB, QStringLiteral("Beta")));
        m_controller->setServiceOptions({});
        m_controller->startLinkedService(taskId);
        QCOMPARE(startSpy.size(), 1);
    }

    void taskAndPlannerPresentationSettingsPersistAfterReopen()
    {
        QVariantMap draft{{QStringLiteral("title"), QStringLiteral("Survives restart")},
            {QStringLiteral("dueDate"), QDate(2026, 11, 2).toString(Qt::ISODate)}};
        QVERIFY(m_controller->saveTask(draft));
        const QString id = m_controller->tasks()->index(0,0).data(presentation::overview::planner::PlannerTaskModel::IdRole).toString();
        m_controller->setView(QStringLiteral("list"));
        m_controller->setCollapsed(true);
        m_controller.reset(); m_service.reset(); m_repository.reset();
        auto close = m_database->close(); QVERIFY2(close, qPrintable(close.error().technicalMessage));
        auto open = m_database->open(); QVERIFY2(open, qPrintable(open.error().technicalMessage));
        createServices();
        m_controller = std::make_unique<presentation::overview::planner::PlannerController>(
            *m_service, *m_settingsService, *m_context, nullptr, nullptr, nullptr, m_activityService.get());
        QCOMPARE(m_controller->totalCount(), 1);
        QCOMPARE(m_controller->tasks()->index(0,0).data(presentation::overview::planner::PlannerTaskModel::IdRole).toString(), id);
        QCOMPARE(m_controller->view(), QStringLiteral("list"));
        QVERIFY(m_controller->collapsed());
        QCOMPARE(m_controller->taskDetails(id).value(QStringLiteral("dueDate")).toString(), QStringLiteral("2026-11-02"));
    }

private:
    domain::workspace::Workspace makeWorkspace(const QString& id, const QString& name) const
    {
        auto workspaceId = domain::workspace::WorkspaceId::fromString(id).value();
        return domain::workspace::Workspace(workspaceId, m_account, name,
            domain::workspace::WorkspacePath(QStringLiteral("C:/%1").arg(name), QStringLiteral("C:/%1").arg(name)),
            domain::workspace::WorkspaceStatus::Ready, false, 0, QDateTime::currentDateTimeUtc());
    }
    void createServices()
    {
        m_repository = std::make_unique<infrastructure::planner::SqlPlannerRepository>(*m_database, m_account);
        m_service = std::make_unique<application::planner::PlannerService>(*m_repository, m_validation, m_ids, m_clock);
        m_activityRepository = std::make_unique<infrastructure::planner::SqlPlannerActivityRepository>(*m_database, m_account);
        m_activityService = std::make_unique<application::planner::PlannerActivityService>(*m_activityRepository, m_clock);
    }
    const QString m_accountId = QStringLiteral("a2222222-2222-4222-8222-222222222222");
    const QString m_workspaceA = QStringLiteral("a1111111-1111-4111-8111-111111111111");
    const QString m_workspaceB = QStringLiteral("b1111111-1111-4111-8111-111111111111");
    std::unique_ptr<QTemporaryDir> m_temp;
    std::unique_ptr<infrastructure::database::InternalDatabaseManager> m_database;
    domain::account::AccountId m_account = domain::account::AccountId::fromString(m_accountId).value();
    shared::identifiers::UuidIdGenerator m_ids;
    shared::time::SystemApplicationClock m_clock;
    application::planner::PlannerValidation m_validation;
    std::unique_ptr<infrastructure::planner::SqlPlannerRepository> m_repository;
    std::unique_ptr<application::planner::PlannerService> m_service;
    std::unique_ptr<infrastructure::planner::SqlPlannerActivityRepository> m_activityRepository;
    std::unique_ptr<application::planner::PlannerActivityService> m_activityService;
    std::unique_ptr<PlannerUiSettings> m_settingsRepository;
    std::unique_ptr<application::settings::SettingsService> m_settingsService;
    std::unique_ptr<application::workspaces::WorkspaceContext> m_context;
    std::unique_ptr<presentation::overview::planner::PlannerController> m_controller;
};

QTEST_GUILESS_MAIN(PlannerUiTest)
#include "planner_ui_test.moc"
