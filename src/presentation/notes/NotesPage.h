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
template<typename T> class QFutureWatcher;

namespace devhub::presentation::notes {

class NotesPage final : public QWidget
{
    Q_OBJECT
public:
    NotesPage(application::notes::INotesRepository& repository, QString accountId, QString recoveryRoot,
              QWidget* parent = nullptr);
    void setWorkspace(QString workspaceId);
    [[nodiscard]] int nodeCount() const { return static_cast<int>(m_nodes.size()); }
    [[nodiscard]] QStringList previewTitles(int limit = 3) const;
    [[nodiscard]] bool flushPendingSave();

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
    void writeRecoveryJournal();
    bool saveNow();
    void updatePreview();
    void showTextCompare(const QString& title, const QString& text);
    void selectNodeInTree(const QString& id);
    void applySearchResults(const QStringList& contentMatches, const QString& needle);
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
    QTimer m_saveTimer;
    QTimer m_journalTimer;
    QString m_currentNoteId;
    QString m_currentContentHash;
    QString m_lastSelectionId;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_includeTrash = false;
    quint64 m_searchGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_searchCancellation;
};

} // namespace devhub::presentation::notes
