#pragma once

#include "../../application/notes/INotesRepository.h"

#include <QWidget>
#include <QStringList>
#include <QHash>
#include <QTimer>
#include <atomic>
#include <memory>
#include <optional>

class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QFrame;
class QVBoxLayout;
template<typename T> class QFutureWatcher;

namespace devhub::application::planner { class TaskNoteLinkService; }

namespace devhub::presentation::notes {

class NotesPage final : public QWidget
{
    Q_OBJECT
public:
    NotesPage(application::notes::INotesRepository& repository, QString accountId, QString recoveryRoot,
              QWidget* parent = nullptr);
    bool setWorkspace(QString workspaceId);
    [[nodiscard]] int nodeCount() const { return static_cast<int>(m_nodes.size()); }
    [[nodiscard]] QStringList previewTitles(int limit = 3) const;
    [[nodiscard]] bool flushPendingSave();
    [[nodiscard]] bool prepareForTransition();
    [[nodiscard]] QString workspaceId() const { return m_workspaceId; }
    [[nodiscard]] QString currentNoteId() const { return m_currentNoteId; }
    [[nodiscard]] bool openNoteById(const QString& noteId);
    void createNoteForPlannerTask(const QString& workspaceId, const QString& taskId);
    void setTaskNoteLinkService(application::planner::TaskNoteLinkService* service);
    void setSessionAllowed(bool allowed);
    void refreshLinkedTaskLinks(const QString& noteId) { refreshLinkedTasks(noteId); }
    [[nodiscard]] bool assistantEditAllowed(const QString& workspaceId, const QString& noteId) const;
    [[nodiscard]] shared::common::Result<QString> applyAssistantEdit(const QString& workspaceId,
        const QString& noteId, const QString& content, const QString& expectedHash);

private:
    void reloadTree();
    void openItem(QTreeWidgetItem* item);
    void createNote(application::notes::NoteNodeKind kind);
    void renameSelected();
    void moveSelected();
    void editTags();
    void togglePin();
    void trashOrRestore();
    void permanentlyDelete();
    void reloadExternalCopy();
    void openNoteExternally();
    void exportNote();
    void exportFolder();
    void scheduleSave();
    bool writeRecoveryJournal();
    bool saveNow();
    void updatePreview();
    void showTextCompare(const QString& title, const QString& text);
    void selectNodeInTree(const QString& id);
    void applySearchResults(const QStringList& contentMatches, const QString& needle);
    void startPendingSearch();
    void refreshLinkedTasks(const QString& noteId);
    void clearLinkedTasks();
    void notifyWorkspaceNotesChanged();
    [[nodiscard]] QString journalPath(const QString& noteId) const;
    [[nodiscard]] QString selectedNodeId() const;
    [[nodiscard]] std::optional<application::notes::NoteNode> selectedNode() const;

    application::notes::INotesRepository& m_repository;
    QString m_accountId;
    QString m_recoveryRoot;
    QString m_workspaceId;
    QHash<QString, application::notes::NoteNode> m_nodes;
    QTreeWidget* m_tree = nullptr;
    QFutureWatcher<QStringList>* m_searchWatcher = nullptr;
    QCheckBox* m_showTrash = nullptr;
    QLineEdit* m_search = nullptr;
    QPlainTextEdit* m_editor = nullptr;
    QTextBrowser* m_preview = nullptr;
    QLabel* m_status = nullptr;
    QFrame* m_linkedTasksPanel = nullptr;
    QVBoxLayout* m_linkedTasksLayout = nullptr;
    application::planner::TaskNoteLinkService* m_taskNoteLinks = nullptr;
    QTimer m_saveTimer;
    QTimer m_journalTimer;
    QString m_currentNoteId;
    QString m_currentContentHash;
    QString m_lastSelectionId;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_includeTrash = false;
    bool m_sessionAllowed = true;
    quint64 m_searchGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_searchCancellation;
    std::optional<QString> m_pendingSearchNeedle;
    QString m_activeSearchNeedle;
    quint64 m_activeSearchGeneration = 0;

signals:
    void openPlannerTaskRequested(QString workspaceId, QString taskId);
    void noteCreatedForPlannerTask(QString workspaceId, QString taskId, QString noteId);
    void taskNoteLinkChanged(QString workspaceId, QString taskId, QString noteId);
    void workspaceNotesChanged(QString workspaceId);
};

} // namespace devhub::presentation::notes
