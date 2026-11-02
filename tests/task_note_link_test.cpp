#include "../src/application/notes/INotesRepository.h"
#include "../src/application/planner/PlannerService.h"
#include "../src/application/planner/TaskNoteLinkService.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/infrastructure/notes/NotesRepository.h"
#include "../src/infrastructure/planner/SqlPlannerRepository.h"
#include "../src/infrastructure/planner/SqlTaskNoteLinkRepository.h"
#include "../src/presentation/notes/NotesPage.h"
#include "../src/shared/identifiers/IdGenerator.h"
#include "../src/shared/time/ApplicationClock.h"

#include <QDir>
#include <QPushButton>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <algorithm>
#include <memory>

using namespace devhub;

class TaskNoteLinkTest final : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        m_temp.reset(new QTemporaryDir(QDir::current().filePath(QStringLiteral("task-note-links-XXXXXX"))));
        QVERIFY(m_temp->isValid());
        m_database.reset(new infrastructure::database::InternalDatabaseManager(
            m_temp->filePath(QStringLiteral("data/devhub.db")), m_temp->filePath(QStringLiteral("backups"))));
        QVERIFY(m_database->open());
        const auto migrated = m_database->migrate();
        QVERIFY2(migrated, qPrintable(migrated.error().technicalMessage));
        QCOMPARE(m_database->currentSchemaVersion(), 13);
        QSqlQuery query(m_database->database());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO accounts(id,username_normalized,display_username,password_hash) "
            "VALUES('%1','link-user','Link User','hash')").arg(m_accountId)));
        for (const auto& entry : {qMakePair(m_workspaceA, QStringLiteral("Alpha")),
                                  qMakePair(m_workspaceB, QStringLiteral("Beta"))}) {
            QVERIFY(query.exec(QStringLiteral("INSERT INTO workspaces(id,account_id,display_name,root_path,entered_path,canonical_root_path) "
                "VALUES('%1','%2','%3','C:/%3','C:/%3','C:/%3')").arg(entry.first, m_accountId, entry.second)));
        }
        const auto account = domain::account::AccountId::fromString(m_accountId);
        QVERIFY(account);
        m_notes.reset(new infrastructure::notes::NotesRepository(*m_database, account.value(),
            m_temp->filePath(QStringLiteral("notes"))));
        m_plannerRepository.reset(new infrastructure::planner::SqlPlannerRepository(*m_database, account.value()));
        m_planner.reset(new application::planner::PlannerService(*m_plannerRepository, m_validation, m_ids, m_clock));
        m_linkRepository.reset(new infrastructure::planner::SqlTaskNoteLinkRepository(*m_database, account.value()));
        m_links.reset(new application::planner::TaskNoteLinkService(*m_linkRepository, *m_planner, *m_notes));
    }

    void cleanup()
    {
        m_links.reset(); m_linkRepository.reset(); m_planner.reset(); m_plannerRepository.reset(); m_notes.reset();
        if (m_database) QVERIFY(m_database->close());
        m_database.reset(); m_temp.reset();
    }

    void linksAreManyToManyUniqueAndFollowMutableNoteMetadata()
    {
        const auto firstTask = createTask(m_workspaceA, QStringLiteral("First task"));
        const auto secondTask = createTask(m_workspaceA, QStringLiteral("Second task"));
        const auto folder = createNode(m_workspaceA, {}, QStringLiteral("Planning"), application::notes::NoteNodeKind::Folder);
        const auto note = createNode(m_workspaceA, folder.id, QStringLiteral("Design notes"), application::notes::NoteNodeKind::Note);
        QVERIFY(m_notes->setTags(m_workspaceA, note.id, {QStringLiteral("architecture"), QStringLiteral("backend")}));
        const auto anotherNote = createNode(m_workspaceA, {}, QStringLiteral("Checklist"), application::notes::NoteNodeKind::Note);
        QSqlQuery folderLink(m_database->database());
        folderLink.prepare(QStringLiteral("INSERT INTO planner_task_note_links(workspace_id,task_id,note_id) VALUES(?,?,?)"));
        folderLink.addBindValue(m_workspaceA); folderLink.addBindValue(firstTask.id); folderLink.addBindValue(folder.id);
        QVERIFY(!folderLink.exec());

        QVERIFY(m_links->link(m_workspaceA, firstTask.id, note.id));
        QVERIFY(m_links->link(m_workspaceA, secondTask.id, note.id));
        QVERIFY(m_links->link(m_workspaceA, firstTask.id, anotherNote.id));
        QVERIFY(!m_links->link(m_workspaceA, firstTask.id, note.id));

        auto firstLinks = m_links->linkedNotesForTask(m_workspaceA, firstTask.id);
        if (!firstLinks) qWarning().noquote() << firstLinks.error().technicalMessage;
        QVERIFY(firstLinks); QCOMPARE(firstLinks.value().size(), 2);
        auto noteLinks = m_links->linkedTasksForNote(m_workspaceA, note.id);
        if (!noteLinks) qWarning().noquote() << noteLinks.error().technicalMessage;
        QVERIFY(noteLinks); QCOMPARE(noteLinks.value().size(), 2);
        QStringList linkedTaskTitles;
        for (const auto& linkedTask : noteLinks.value()) linkedTaskTitles.append(linkedTask.title);
        linkedTaskTitles.sort();
        QStringList expectedTaskTitles{QStringLiteral("First task"), QStringLiteral("Second task")};
        expectedTaskTitles.sort();
        QCOMPARE(linkedTaskTitles, expectedTaskTitles);

        const auto search = m_links->searchNotes(m_workspaceA, firstTask.id, QStringLiteral("architecture"));
        QVERIFY(search); QCOMPARE(search.value().size(), 1);
        QVERIFY(search.value().front().alreadyLinked);
        QCOMPARE(search.value().front().folderPath, QStringLiteral("Planning"));
        QStringList actualTags = search.value().front().tags;
        actualTags.sort();
        QStringList expectedTags{QStringLiteral("architecture"), QStringLiteral("backend")};
        expectedTags.sort();
        QCOMPARE(actualTags, expectedTags);

        QVERIFY(m_notes->rename(m_workspaceA, note.id, QStringLiteral("Revised design")));
        const auto movedFolder = createNode(m_workspaceA, {}, QStringLiteral("Specs"), application::notes::NoteNodeKind::Folder);
        QVERIFY(m_notes->move(m_workspaceA, note.id, movedFolder.id));
        const auto renamedLinks = m_links->linkedNotesForTask(m_workspaceA, firstTask.id);
        QVERIFY(renamedLinks);
        const auto renamed = std::find_if(renamedLinks.value().cbegin(), renamedLinks.value().cend(),
            [&note](const auto& item) { return item.noteId == note.id; });
        QVERIFY(renamed != renamedLinks.value().cend());
        QCOMPARE(renamed->title, QStringLiteral("Revised design"));
        QCOMPARE(renamed->folderPath, QStringLiteral("Specs"));

        QVERIFY(m_links->unlink(m_workspaceA, firstTask.id, note.id));
        QCOMPARE(m_links->linkedNotesForTask(m_workspaceA, firstTask.id).value().size(), 1);
        QCOMPARE(m_links->linkedTasksForNote(m_workspaceA, note.id).value().size(), 1);
        QVERIFY(m_notes->readContent(m_workspaceA, note.id));
    }

    void trashRestoreAndPermanentDeletionPreserveExpectedRelationships()
    {
        const auto firstTask = createTask(m_workspaceA, QStringLiteral("Keep task"));
        const auto secondTask = createTask(m_workspaceA, QStringLiteral("Other task"));
        const auto note = createNode(m_workspaceA, {}, QStringLiteral("Reference"), application::notes::NoteNodeKind::Note);
        QVERIFY(m_links->link(m_workspaceA, firstTask.id, note.id));
        QVERIFY(m_notes->setTrashed(m_workspaceA, note.id, true));

        auto trashedLinks = m_links->linkedNotesForTask(m_workspaceA, firstTask.id);
        if (!trashedLinks) qWarning().noquote() << trashedLinks.error().technicalMessage;
        QVERIFY(trashedLinks); QCOMPARE(trashedLinks.value().size(), 1);
        QVERIFY(trashedLinks.value().front().trashed);
        QVERIFY(!m_links->link(m_workspaceA, secondTask.id, note.id));
        const auto candidates = m_links->searchNotes(m_workspaceA, secondTask.id, QString{});
        QVERIFY(candidates);
        QVERIFY(std::none_of(candidates.value().cbegin(), candidates.value().cend(),
            [&note](const auto& candidate) { return candidate.noteId == note.id; }));

        QVERIFY(m_notes->setTrashed(m_workspaceA, note.id, false));
        const auto restoredLinks = m_links->linkedNotesForTask(m_workspaceA, firstTask.id);
        QVERIFY(restoredLinks); QCOMPARE(restoredLinks.value().size(), 1);
        QVERIFY(!restoredLinks.value().front().trashed);
        QVERIFY(m_links->link(m_workspaceA, secondTask.id, note.id));

        QVERIFY(m_notes->setTrashed(m_workspaceA, note.id, true));
        QVERIFY(m_notes->permanentlyRemove(m_workspaceA, note.id));
        QCOMPARE(linkCount(), 0);
        QVERIFY(!m_links->linkedTasksForNote(m_workspaceA, note.id));
        QVERIFY(m_planner->getTask(m_workspaceA, firstTask.id));
        QVERIFY(m_planner->getTask(m_workspaceA, secondTask.id));
    }

    void taskDeletionAndWorkspaceBoundariesKeepNotesSafe()
    {
        const auto taskA = createTask(m_workspaceA, QStringLiteral("Workspace A task"));
        const auto taskB = createTask(m_workspaceB, QStringLiteral("Workspace B task"));
        const auto noteA = createNode(m_workspaceA, {}, QStringLiteral("A note"), application::notes::NoteNodeKind::Note);
        const auto noteB = createNode(m_workspaceB, {}, QStringLiteral("B note"), application::notes::NoteNodeKind::Note);
        QVERIFY(m_links->link(m_workspaceA, taskA.id, noteA.id));
        QVERIFY(!m_links->link(m_workspaceA, taskA.id, noteB.id));
        QVERIFY(!m_links->link(m_workspaceA, taskB.id, noteA.id));

        auto otherAccountId = domain::account::AccountId::fromString(QStringLiteral("a2222222-2222-4222-8222-222222222222"));
        QVERIFY(otherAccountId);
        infrastructure::planner::SqlTaskNoteLinkRepository otherAccountLinks(*m_database, otherAccountId.value());
        QVERIFY(!otherAccountLinks.requireActiveWorkspace(m_workspaceA));
        QVERIFY(!otherAccountLinks.add(m_workspaceA, taskA.id, noteA.id));
        const auto hidden = otherAccountLinks.listForTask(m_workspaceA, taskA.id);
        if (!hidden) qWarning().noquote() << hidden.error().technicalMessage;
        QVERIFY(hidden); QVERIFY(hidden.value().isEmpty());

        QVERIFY(m_planner->removeTask(m_workspaceA, taskA.id, taskA.revision));
        QCOMPARE(linkCount(), 0);
        QVERIFY(m_notes->readContent(m_workspaceA, noteA.id));
        const auto noTasks = m_links->linkedTasksForNote(m_workspaceA, noteA.id);
        if (!noTasks) qWarning().noquote() << noTasks.error().technicalMessage;
        QVERIFY(noTasks); QCOMPARE(noTasks.value().size(), 0);
    }

    void archiveBlocksLinkOperationsAndRestoreRevealsPreservedLinks()
    {
        const auto task = createTask(m_workspaceA, QStringLiteral("Archived task"));
        const auto note = createNode(m_workspaceA, {}, QStringLiteral("Archived workspace note"), application::notes::NoteNodeKind::Note);
        QVERIFY(m_links->link(m_workspaceA, task.id, note.id));
        QSqlQuery query(m_database->database());
        QVERIFY(query.prepare(QStringLiteral("UPDATE workspaces SET archived_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?")));
        query.addBindValue(m_workspaceA); QVERIFY(query.exec());
        QVERIFY(!m_links->linkedNotesForTask(m_workspaceA, task.id));
        QVERIFY(!m_links->unlink(m_workspaceA, task.id, note.id));
        QCOMPARE(linkCount(), 1);
        QVERIFY(query.prepare(QStringLiteral("UPDATE workspaces SET archived_at_utc=NULL WHERE id=?")));
        query.addBindValue(m_workspaceA); QVERIFY(query.exec());
        const auto restored = m_links->linkedNotesForTask(m_workspaceA, task.id);
        QVERIFY(restored); QCOMPARE(restored.value().size(), 1);
        QCOMPARE(restored.value().front().noteId, note.id);
    }

    void notesPageNavigatesByStableIdAndShowsReverseTaskLinks()
    {
        const auto task = createTask(m_workspaceA, QStringLiteral("Navigate to task"));
        const auto note = createNode(m_workspaceA, {}, QStringLiteral("Specific note"), application::notes::NoteNodeKind::Note);
        const auto other = createNode(m_workspaceA, {}, QStringLiteral("Other note"), application::notes::NoteNodeKind::Note);
        QVERIFY(m_links->link(m_workspaceA, task.id, note.id));
        presentation::notes::NotesPage page(*m_notes, m_accountId, m_temp->path());
        page.setTaskNoteLinkService(m_links.get());
        QVERIFY(page.setWorkspace(m_workspaceA));
        QSignalSpy openTask(&page, &presentation::notes::NotesPage::openPlannerTaskRequested);
        QVERIFY(page.openNoteById(note.id));
        QCOMPARE(page.currentNoteId(), note.id);
        const auto buttons = page.findChildren<QPushButton*>();
        auto taskButton = std::find_if(buttons.cbegin(), buttons.cend(), [](const QPushButton* button) {
            return button->text() == QStringLiteral("Open task");
        });
        QVERIFY(taskButton != buttons.cend());
        (*taskButton)->click();
        QCOMPARE(openTask.count(), 1);
        QCOMPARE(openTask.takeFirst().at(1).toString(), task.id);
        const auto unlinkButton = std::find_if(buttons.cbegin(), buttons.cend(), [](const QPushButton* button) {
            return button->text() == QStringLiteral("Unlink");
        });
        QVERIFY(unlinkButton != buttons.cend());
        (*unlinkButton)->click();
        QVERIFY(m_links->linkedTasksForNote(m_workspaceA, note.id).value().isEmpty());
        QVERIFY(page.openNoteById(other.id));
        QCOMPARE(page.currentNoteId(), other.id);
    }

private:
    domain::planner::PlannerTask createTask(const QString& workspace, const QString& title)
    {
        domain::planner::PlannerTask task; task.title = title;
        const auto created = m_planner->createTask(workspace, task);
        if (!created) qFatal("Could not create test task: %s", qPrintable(created.error().technicalMessage));
        return created.value();
    }

    application::notes::NoteNode createNode(const QString& workspace, const QString& parent,
        const QString& title, application::notes::NoteNodeKind kind)
    {
        const auto created = m_notes->create(workspace, parent, title, kind);
        if (!created) qFatal("Could not create test note: %s", qPrintable(created.error().technicalMessage));
        return created.value();
    }

    int linkCount() const
    {
        QSqlQuery query(m_database->database());
        if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM planner_task_note_links")) || !query.next()) return -1;
        return query.value(0).toInt();
    }

    const QString m_accountId{QStringLiteral("a1111111-1111-4111-8111-111111111111")};
    const QString m_workspaceA{QStringLiteral("b1111111-1111-4111-8111-111111111111")};
    const QString m_workspaceB{QStringLiteral("b2222222-2222-4222-8222-222222222222")};
    std::unique_ptr<QTemporaryDir> m_temp;
    std::unique_ptr<infrastructure::database::InternalDatabaseManager> m_database;
    std::unique_ptr<infrastructure::notes::NotesRepository> m_notes;
    std::unique_ptr<infrastructure::planner::SqlPlannerRepository> m_plannerRepository;
    application::planner::PlannerValidation m_validation;
    shared::identifiers::UuidIdGenerator m_ids;
    shared::time::SystemApplicationClock m_clock;
    std::unique_ptr<application::planner::PlannerService> m_planner;
    std::unique_ptr<infrastructure::planner::SqlTaskNoteLinkRepository> m_linkRepository;
    std::unique_ptr<application::planner::TaskNoteLinkService> m_links;
};

QTEST_MAIN(TaskNoteLinkTest)
#include "task_note_link_test.moc"
