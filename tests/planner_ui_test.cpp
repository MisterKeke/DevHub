#include "../src/application/planner/PlannerService.h"
#include "../src/application/settings/SettingsService.h"
#include "../src/application/workspaces/WorkspaceContext.h"
#include "../src/domain/errors/AppError.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/infrastructure/planner/SqlPlannerRepository.h"
#include "../src/presentation/overview/planner/PlannerController.h"
#include "../src/shared/identifiers/IdGenerator.h"
#include "../src/shared/time/ApplicationClock.h"

#include <QDir>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

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
            *m_service, *m_settingsService, *m_context);
    }

    void cleanup()
    {
        m_controller.reset(); m_context.reset(); m_settingsService.reset(); m_settingsRepository.reset();
        m_service.reset(); m_repository.reset();
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
        const QString taskId = m_controller->tasks()->index(0, 0).data(PlannerTaskModel::IdRole).toString();
        QVERIFY(!taskId.isEmpty());
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
            *m_service, *m_settingsService, *m_context);
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
    std::unique_ptr<PlannerUiSettings> m_settingsRepository;
    std::unique_ptr<application::settings::SettingsService> m_settingsService;
    std::unique_ptr<application::workspaces::WorkspaceContext> m_context;
    std::unique_ptr<presentation::overview::planner::PlannerController> m_controller;
};

QTEST_GUILESS_MAIN(PlannerUiTest)
#include "planner_ui_test.moc"
