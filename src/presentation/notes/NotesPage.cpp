#include "NotesPage.h"

#include <QCryptographicHash>
#include <QCheckBox>
#include <QDir>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTextDocument>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QColor>
#include <QTime>
#include <QTreeWidget>
#include <QUrl>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>
#include <QVBoxLayout>

namespace devhub::presentation::notes {
using namespace application::notes;
namespace {
constexpr int NodeIdRole = Qt::UserRole + 1;
constexpr int NodeKindRole = Qt::UserRole + 2;
QString contentHash(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

class MarkdownHighlighter final : public QSyntaxHighlighter
{
public:
    explicit MarkdownHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}
protected:
    void highlightBlock(const QString& text) override
    {
        QTextCharFormat heading; heading.setForeground(QColor(QStringLiteral("#78a9ff"))); heading.setFontWeight(QFont::Bold);
        static const QRegularExpression headingPattern(QStringLiteral("^#{1,6}\\s.*$"));
        if (headingPattern.match(text).hasMatch()) setFormat(0, static_cast<int>(text.size()), heading);
        QTextCharFormat code; code.setForeground(QColor(QStringLiteral("#7ee0c3")));
        static const QRegularExpression inlineCode(QStringLiteral("`[^`]*`"));
        auto match = inlineCode.globalMatch(text);
        while (match.hasNext()) { const auto found = match.next(); setFormat(static_cast<int>(found.capturedStart()), static_cast<int>(found.capturedLength()), code); }
        QTextCharFormat bold; bold.setFontWeight(QFont::Bold);
        static const QRegularExpression boldPattern(QStringLiteral("\\*\\*[^*]+\\*\\*"));
        match = boldPattern.globalMatch(text);
        while (match.hasNext()) { const auto found = match.next(); setFormat(static_cast<int>(found.capturedStart()), static_cast<int>(found.capturedLength()), bold); }
    }
};

class SafeMarkdownPreview final : public QTextBrowser
{
public:
    using QTextBrowser::QTextBrowser;
protected:
    QVariant loadResource(int type, const QUrl& name) override
    {
        if (name.scheme() == QStringLiteral("qrc")) return QTextBrowser::loadResource(type, name);
        return {};
    }
};
}

NotesPage::NotesPage(INotesRepository& repository, QString accountId, QString recoveryRoot, QWidget* parent)
    : QWidget(parent), m_repository(repository), m_accountId(std::move(accountId)), m_recoveryRoot(std::move(recoveryRoot))
{
    auto* root = new QVBoxLayout(this); root->setContentsMargins(24, 20, 24, 20);
    auto* toolbar = new QHBoxLayout();
    m_search = new QLineEdit(this); m_search->setObjectName(QStringLiteral("notesSearch"));
    m_search->setPlaceholderText(tr("Search titles, tags, and note text")); m_search->setClearButtonEnabled(true);
    auto* newNote = new QPushButton(tr("New Note"), this); auto* newFolder = new QPushButton(tr("New Folder"), this);
    auto* rename = new QPushButton(tr("Rename"), this); auto* move = new QPushButton(tr("Move…"), this);
    auto* pin = new QPushButton(tr("Pin / Unpin"), this); auto* tags = new QPushButton(tr("Tags…"), this);
    auto* trash = new QPushButton(tr("Trash / Restore"), this); auto* deleteForever = new QPushButton(tr("Delete Permanently…"), this);
    auto* reloadExternal = new QPushButton(tr("Reload External"), this); auto* openExternal = new QPushButton(tr("Open Note File"), this);
    auto* exportNoteButton = new QPushButton(tr("Export Note"), this); auto* exportFolderButton = new QPushButton(tr("Export Folder"), this);
    m_showTrash = new QCheckBox(tr("Show Trash"), this);
    toolbar->addWidget(m_search, 1); toolbar->addWidget(newNote); toolbar->addWidget(newFolder); toolbar->addWidget(m_showTrash); root->addLayout(toolbar);
    auto* actions = new QHBoxLayout();
    actions->addWidget(rename); actions->addWidget(move); actions->addWidget(pin); actions->addWidget(tags);
    actions->addWidget(trash); actions->addWidget(deleteForever); actions->addWidget(reloadExternal); actions->addWidget(openExternal);
    actions->addWidget(exportNoteButton); actions->addWidget(exportFolderButton); actions->addStretch(); root->addLayout(actions);
    auto* split = new QSplitter(Qt::Horizontal, this);
    m_tree = new QTreeWidget(split); m_tree->setObjectName(QStringLiteral("notesTree"));
    m_tree->setHeaderLabel(tr("Workspace Notes")); m_tree->setMinimumWidth(220);
    auto* editorPane = new QSplitter(Qt::Horizontal, split);
    m_editor = new QPlainTextEdit(editorPane); m_editor->setObjectName(QStringLiteral("markdownEditor"));
    m_editor->setPlaceholderText(tr("Select a note or create one to start writing Markdown."));
    m_preview = new SafeMarkdownPreview(editorPane); m_preview->setObjectName(QStringLiteral("markdownPreview"));
    m_preview->setOpenLinks(false); m_preview->setOpenExternalLinks(false);
    new MarkdownHighlighter(m_editor->document());
    m_preview->setPlaceholderText(tr("Markdown preview — remote links are not opened."));
    editorPane->addWidget(m_editor); editorPane->addWidget(m_preview); editorPane->setStretchFactor(0, 1); editorPane->setStretchFactor(1, 1);
    split->addWidget(m_tree); split->addWidget(editorPane); split->setStretchFactor(1, 1); root->addWidget(split, 1);
    m_status = new QLabel(tr("Choose a workspace to view its notes."), this); root->addWidget(m_status);
    m_saveTimer.setSingleShot(true); m_saveTimer.setInterval(700);
    m_journalTimer.setSingleShot(true); m_journalTimer.setInterval(350);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveNow(); });
    connect(&m_journalTimer, &QTimer::timeout, this, &NotesPage::writeRecoveryJournal);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &NotesPage::scheduleSave);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) { openItem(item); });
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        const quint64 generation = ++m_searchGeneration;
        if (m_searchCancellation) m_searchCancellation->store(true);
        const QString needle = m_search->text().trimmed();
        if (needle.isEmpty()) { applySearchResults({}, needle); return; }
        QVector<QPair<QString, QString>> sources;
        for (const auto& node : std::as_const(m_nodes)) if (node.kind == NoteNodeKind::Note)
            sources.append({node.id, m_repository.contentPath(m_workspaceId, node.id)});
        auto* watcher = new QFutureWatcher<QStringList>(this);
        auto cancellation = std::make_shared<std::atomic_bool>(false);
        m_searchCancellation = cancellation;
        connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, generation, needle] {
            const QStringList matches = watcher->result(); watcher->deleteLater();
            if (generation == m_searchGeneration) applySearchResults(matches, needle);
        });
        watcher->setFuture(QtConcurrent::run([sources = std::move(sources), needle, cancellation] {
            QStringList matches;
            for (const auto& source : sources) {
                if (cancellation->load()) break;
                QFileInfo info(source.second); if (!info.exists() || info.size() > 2 * 1024 * 1024) continue;
                QFile file(source.second); if (!file.open(QIODevice::ReadOnly)) continue;
                if (QString::fromUtf8(file.readAll()).contains(needle, Qt::CaseInsensitive)) matches.append(source.first);
            }
            return matches;
        }));
        applySearchResults({}, needle);
    });
    connect(newNote, &QPushButton::clicked, this, [this] { createNote(NoteNodeKind::Note); });
    connect(newFolder, &QPushButton::clicked, this, [this] { createNote(NoteNodeKind::Folder); });
    connect(rename, &QPushButton::clicked, this, &NotesPage::renameSelected);
    connect(move, &QPushButton::clicked, this, &NotesPage::moveSelected);
    connect(pin, &QPushButton::clicked, this, &NotesPage::togglePin);
    connect(tags, &QPushButton::clicked, this, &NotesPage::editTags);
    connect(trash, &QPushButton::clicked, this, &NotesPage::trashOrRestore);
    connect(deleteForever, &QPushButton::clicked, this, &NotesPage::permanentlyDelete);
    connect(reloadExternal, &QPushButton::clicked, this, &NotesPage::reloadExternalCopy);
    connect(openExternal, &QPushButton::clicked, this, &NotesPage::openNoteExternally);
    connect(exportNoteButton, &QPushButton::clicked, this, &NotesPage::exportNote);
    connect(exportFolderButton, &QPushButton::clicked, this, &NotesPage::exportFolder);
    connect(m_showTrash, &QCheckBox::toggled, this, [this](bool enabled) { m_includeTrash = enabled; reloadTree(); });
}

void NotesPage::setWorkspace(QString workspaceId)
{
    if (m_workspaceId == workspaceId) return;
    ++m_searchGeneration;
    const bool saved = flushPendingSave();
    Q_UNUSED(saved);
    m_saveTimer.stop(); m_journalTimer.stop();
    if (m_searchCancellation) m_searchCancellation->store(true);
    m_workspaceId = std::move(workspaceId); m_currentNoteId.clear(); m_currentContentHash.clear();
    m_dirty = false;
    m_editor->clear(); m_preview->clear(); reloadTree();
}

bool NotesPage::flushPendingSave()
{
    m_saveTimer.stop();
    if (!m_dirty) return true;
    return saveNow();
}

void NotesPage::reloadTree()
{
    m_tree->clear(); m_nodes.clear();
    if (m_workspaceId.isEmpty()) { m_status->setText(tr("Choose a workspace to view its notes.")); return; }
    const auto result = m_repository.list(m_workspaceId, m_includeTrash);
    if (!result) { m_status->setText(result.error().userMessage); return; }
    const auto nodes = result.value();
    QHash<QString, QTreeWidgetItem*> items;
    for (const auto& node : nodes) m_nodes.insert(node.id, node);
    QTreeWidgetItem* root = new QTreeWidgetItem(m_tree, {tr("Workspace Notes")}); root->setData(0, NodeIdRole, QString());
    QVector<NoteNode> pending = nodes;
    while (!pending.isEmpty()) {
        bool moved = false;
        for (qsizetype i = 0; i < pending.size();) {
            const NoteNode node = pending.at(i);
            QTreeWidgetItem* parent = node.parentId.isEmpty() ? root : items.value(node.parentId, nullptr);
            if (!parent) { ++i; continue; }
            QString title = node.title;
            if (node.pinned) title = QStringLiteral("★ ") + title;
            if (node.trashed) title = QStringLiteral("[Trash] ") + title;
            if (!node.tags.isEmpty()) title += QStringLiteral("  [%1]").arg(node.tags.join(QStringLiteral(", ")));
            auto* item = new QTreeWidgetItem(parent, {title});
            item->setData(0, NodeIdRole, node.id); item->setData(0, NodeKindRole, static_cast<int>(node.kind));
            if (node.kind == NoteNodeKind::Folder) item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            else item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
            items.insert(node.id, item); pending.removeAt(i); moved = true;
        }
        if (!moved) break;
    }
    root->setExpanded(true); m_tree->expandToDepth(1);
    m_status->setText(tr("%1 note items. Changes save automatically.").arg(nodes.size()));
}

QStringList NotesPage::previewTitles(int limit) const
{
    QStringList titles;
    for (const auto& node : m_nodes) {
        if (node.kind == application::notes::NoteNodeKind::Note && !node.trashed)
            titles.append(node.title);
    }
    titles.sort(Qt::CaseInsensitive);
    if (limit >= 0 && titles.size() > limit) titles = titles.mid(0, limit);
    return titles;
}

QString NotesPage::selectedNodeId() const { return m_tree->currentItem() ? m_tree->currentItem()->data(0, NodeIdRole).toString() : QString(); }
std::optional<NoteNode> NotesPage::selectedNode() const
{
    const QString id = selectedNodeId(); if (!m_nodes.contains(id)) return std::nullopt; return m_nodes.value(id);
}

void NotesPage::openItem(QTreeWidgetItem* item)
{
    if (!item) return;
    const QString id = item->data(0, NodeIdRole).toString();
    if (id.isEmpty() || !m_nodes.contains(id) || m_nodes.value(id).kind == NoteNodeKind::Folder) return;
    if (id == m_currentNoteId) return;
    if (!flushPendingSave()) { selectNodeInTree(m_currentNoteId); m_status->setText(tr("The current note could not be saved; its recovery journal was kept.")); return; }
    const NoteNode node = m_nodes.value(id);
    auto contentResult = m_repository.readContent(m_workspaceId, id);
    if (!contentResult) { m_status->setText(contentResult.error().userMessage); return; }
    const QString text = contentResult.value();
    const QByteArray bytes = text.toUtf8();
    const QString actualHash = contentHash(bytes);
    if (!node.contentHash.isEmpty() && node.contentHash != actualHash) {
        QMessageBox box(QMessageBox::Warning, tr("Note changed outside DevHub"),
            tr("The note file differs from the last version saved by DevHub."), QMessageBox::NoButton, this);
        auto* compare = box.addButton(tr("View External Copy"), QMessageBox::ActionRole);
        auto* reload = box.addButton(tr("Reload External Copy"), QMessageBox::AcceptRole);
        auto* cancel = box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == cancel) { selectNodeInTree(m_currentNoteId); return; }
        if (box.clickedButton() == reload) { /* open the current on-disk version below */ }
        if (box.clickedButton() == compare) {
            showTextCompare(tr("External note contents"), text);
            selectNodeInTree(m_currentNoteId);
            m_status->setText(tr("External version shown. Use Reload External to load it into the editor."));
            return;
        }
    }
    m_loading = true; m_currentNoteId = id; m_lastSelectionId = id; m_currentContentHash = actualHash;
    m_editor->setPlainText(text); m_loading = false; m_dirty = false;
    const QString journal = journalPath(id); QFile recovery(journal);
    const QString filePath = m_repository.contentPath(m_workspaceId, id);
    if (recovery.exists() && QFileInfo(journal).lastModified() > QFileInfo(filePath).lastModified() && recovery.open(QIODevice::ReadOnly)) {
        const QString recovered = QString::fromUtf8(recovery.readAll());
        QMessageBox box(QMessageBox::Question, tr("Recover unsaved note?"),
            tr("A newer recovery copy is available. Restore it, compare it, or discard it?"), QMessageBox::NoButton, this);
        auto* restore = box.addButton(tr("Restore"), QMessageBox::AcceptRole);
        auto* compare = box.addButton(tr("Compare"), QMessageBox::ActionRole);
        auto* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        auto* keep = box.addButton(tr("Keep Current"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == restore) { m_editor->setPlainText(recovered); m_dirty = true; scheduleSave(); }
        else if (box.clickedButton() == compare) showTextCompare(tr("Recovered note copy"), recovered);
        else if (box.clickedButton() == discard) QFile::remove(journal);
        Q_UNUSED(keep);
    }
    updatePreview(); m_status->setText(tr("Opened %1").arg(node.title));
}

void NotesPage::createNote(NoteNodeKind kind)
{
    if (m_workspaceId.isEmpty()) { m_status->setText(tr("Choose a workspace first.")); return; }
    if (!flushPendingSave()) return;
    QString suggested = kind == NoteNodeKind::Folder ? tr("New Folder") : tr("Untitled Note");
    bool accepted = false;
    const QString title = QInputDialog::getText(this, kind == NoteNodeKind::Folder ? tr("Create Folder") : tr("Create Note"),
        tr("Title"), QLineEdit::Normal, suggested, &accepted);
    if (!accepted) return;
    QString parentId;
    if (const auto selected = selectedNode(); selected && (selected->kind == NoteNodeKind::Folder || selected->trashed == false))
        parentId = selected->kind == NoteNodeKind::Folder ? selected->id : selected->parentId;
    const auto created = m_repository.create(m_workspaceId, parentId, title, kind);
    if (!created) { QMessageBox::warning(this, tr("Could not create note item"), created.error().userMessage); return; }
    reloadTree();
    selectNodeInTree(created.value().id);
}

void NotesPage::renameSelected()
{
    const auto node = selectedNode(); if (!node) return;
    if (node->id == m_currentNoteId && !flushPendingSave()) return;
    bool accepted = false; const QString title = QInputDialog::getText(this, tr("Rename"), tr("Title"), QLineEdit::Normal, node->title, &accepted);
    if (!accepted) return;
    auto result = m_repository.rename(m_workspaceId, node->id, title);
    if (!result) QMessageBox::warning(this, tr("Rename failed"), result.error().userMessage); else reloadTree();
}

void NotesPage::moveSelected()
{
    const auto node = selectedNode(); if (!node) return;
    if (!flushPendingSave()) return;
    QStringList folderLabels{tr("Workspace root")}; QStringList folderIds{QString()};
    for (const auto& item : std::as_const(m_nodes)) if (item.kind == NoteNodeKind::Folder && item.id != node->id) { folderLabels.append(item.title); folderIds.append(item.id); }
    bool accepted = false; const QString choice = QInputDialog::getItem(this, tr("Move Note Item"), tr("Destination folder"), folderLabels, 0, false, &accepted);
    if (!accepted) return;
    auto result = m_repository.move(m_workspaceId, node->id, folderIds.value(folderLabels.indexOf(choice)));
    if (!result) QMessageBox::warning(this, tr("Move failed"), result.error().userMessage); else reloadTree();
}

void NotesPage::editTags()
{
    const auto node = selectedNode(); if (!node || node->kind != NoteNodeKind::Note) return;
    if (node->id == m_currentNoteId && !flushPendingSave()) return;
    bool accepted = false; const QString input = QInputDialog::getText(this, tr("Edit Note Tags"), tr("Comma-separated tags"), QLineEdit::Normal, node->tags.join(QStringLiteral(", ")), &accepted);
    if (!accepted) return;
    auto result = m_repository.setTags(m_workspaceId, node->id, input.split(QLatin1Char(','), Qt::SkipEmptyParts));
    if (!result) QMessageBox::warning(this, tr("Could not save tags"), result.error().userMessage); else reloadTree();
}

void NotesPage::togglePin()
{
    const auto node = selectedNode(); if (!node || node->kind != NoteNodeKind::Note) return;
    if (node->id == m_currentNoteId && !flushPendingSave()) return;
    auto result = m_repository.setPinned(m_workspaceId, node->id, !node->pinned);
    if (!result) QMessageBox::warning(this, tr("Could not update pin"), result.error().userMessage); else reloadTree();
}

void NotesPage::trashOrRestore()
{
    const auto node = selectedNode(); if (!node) return;
    if (!flushPendingSave()) return;
    const bool trashed = node->trashed;
    if (!trashed && QMessageBox::question(this, tr("Move to DevHub Trash"), tr("Move “%1” to recoverable trash?").arg(node->title)) != QMessageBox::Yes) return;
    auto result = m_repository.setTrashed(m_workspaceId, node->id, !trashed);
    if (!result) QMessageBox::warning(this, tr("Could not update trash"), result.error().userMessage); else reloadTree();
}

void NotesPage::permanentlyDelete()
{
    const auto node = selectedNode(); if (!node || !node->trashed) return;
    if (!flushPendingSave()) return;
    if (QMessageBox::warning(this, tr("Permanently Delete"), tr("Delete “%1” and its contents permanently? This cannot be undone.").arg(node->title), QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
    const auto allNodes = m_repository.list(m_workspaceId, true);
    QStringList affectedIds{node->id};
    if (allNodes) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& candidate : allNodes.value())
                if (affectedIds.contains(candidate.parentId) && !affectedIds.contains(candidate.id)) { affectedIds.append(candidate.id); changed = true; }
        }
    }
    auto result = m_repository.permanentlyRemove(m_workspaceId, node->id);
    if (!result) QMessageBox::warning(this, tr("Delete failed"), result.error().userMessage);
    else { for (const auto& id : affectedIds) QFile::remove(journalPath(id)); reloadTree(); }
}

void NotesPage::reloadExternalCopy()
{
    if (m_currentNoteId.isEmpty()) return;
    if (m_dirty && QMessageBox::warning(this, tr("Discard pending edits?"),
            tr("Reloading replaces your current editor contents. The recovery journal will be discarded."),
            QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
    const QString path = m_repository.contentPath(m_workspaceId, m_currentNoteId);
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { m_status->setText(tr("Could not reload the note: %1").arg(file.errorString())); return; }
    const QByteArray bytes = file.readAll();
    m_loading = true; m_editor->setPlainText(QString::fromUtf8(bytes)); m_loading = false;
    m_currentContentHash = contentHash(bytes); m_dirty = false; m_saveTimer.stop(); m_journalTimer.stop();
    QFile::remove(journalPath(m_currentNoteId)); updatePreview();
    m_status->setText(tr("Reloaded the file currently on disk."));
}

void NotesPage::openNoteExternally()
{
    const auto node = selectedNode(); if (!node || node->kind != NoteNodeKind::Note || !flushPendingSave()) return;
    const QString path = m_repository.contentPath(m_workspaceId, node->id);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) m_status->setText(tr("The system could not open the note file."));
}

void NotesPage::exportNote()
{
    const auto node = selectedNode(); if (!node || node->kind != NoteNodeKind::Note) return;
    if (node->id == m_currentNoteId && !flushPendingSave()) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Note"), node->title + QStringLiteral(".md"), tr("Markdown (*.md);;All files (*)"));
    if (path.isEmpty()) return;
    const auto content = m_repository.readContent(m_workspaceId, node->id);
    if (!content) { QMessageBox::warning(this, tr("Export failed"), content.error().userMessage); return; }
    QSaveFile file(path); const QByteArray bytes = content.value().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
}

void NotesPage::exportFolder()
{
    const auto selected = selectedNode(); if (!selected || selected->kind != NoteNodeKind::Folder) return;
    const QString destination = QFileDialog::getExistingDirectory(this, tr("Export Folder")); if (destination.isEmpty()) return;
    const auto listed = m_repository.list(m_workspaceId, false);
    if (!listed) { QMessageBox::warning(this, tr("Export failed"), listed.error().userMessage); return; }
    QHash<QString, QString> relative;
    const auto safeSegment = [](QString value) {
        value.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
        while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' '))) value.chop(1);
        if (value.isEmpty()) value = QStringLiteral("Untitled");
        return value;
    };
    const QString exportRoot = QDir(destination).filePath(safeSegment(selected->title));
    if (QFileInfo::exists(exportRoot)) { QMessageBox::warning(this, tr("Export path exists"), tr("Choose a destination where “%1” does not already exist.").arg(safeSegment(selected->title))); return; }
    relative.insert(selected->id, safeSegment(selected->title));
    for (int pass = 0; pass < listed.value().size(); ++pass) {
        bool changed = false;
        for (const auto& node : listed.value()) {
            if (relative.contains(node.id) || !relative.contains(node.parentId)) continue;
            relative.insert(node.id, QDir(relative.value(node.parentId)).filePath(safeSegment(node.title)));
            changed = true;
        }
        if (!changed) break;
    }
    for (const auto& node : listed.value()) {
        if (node.kind != NoteNodeKind::Folder || !relative.contains(node.id)) continue;
        const QString path = QDir(destination).filePath(relative.value(node.id));
        if (!QDir().mkpath(path)) { m_status->setText(tr("Folder export stopped: could not create %1").arg(path)); return; }
    }
    for (const auto& node : listed.value()) {
        if (!relative.contains(node.id)) continue;
        const QString path = QDir(destination).filePath(relative.value(node.id) + (node.kind == NoteNodeKind::Note ? QStringLiteral(".md") : QString()));
        if (node.kind == NoteNodeKind::Note) {
            const auto content = m_repository.readContent(m_workspaceId, node.id);
            if (!content) { m_status->setText(content.error().userMessage); return; }
            QSaveFile file(path); const QByteArray bytes = content.value().toUtf8();
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { m_status->setText(tr("Folder export stopped: %1").arg(file.errorString())); return; }
        }
    }
    m_status->setText(tr("Exported folder “%1” to %2.").arg(selected->title, destination));
}

void NotesPage::scheduleSave()
{
    if (m_loading || m_currentNoteId.isEmpty()) return;
    m_dirty = true; updatePreview(); m_saveTimer.start(); m_journalTimer.start();
    m_status->setText(tr("Unsaved changes — autosaving…"));
}

QString NotesPage::journalPath(const QString& noteId) const
{
    return QDir(m_recoveryRoot).filePath(QStringLiteral(".recovery/%1/%2/%3.md").arg(m_accountId, m_workspaceId, noteId));
}

void NotesPage::selectNodeInTree(const QString& id)
{
    if (id.isEmpty()) { m_tree->setCurrentItem(nullptr); return; }
    QList<QTreeWidgetItem*> stack;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) stack.append(m_tree->topLevelItem(i));
    while (!stack.isEmpty()) {
        auto* item = stack.takeLast();
        if (item->data(0, NodeIdRole).toString() == id) { m_tree->setCurrentItem(item); return; }
        for (int i = 0; i < item->childCount(); ++i) stack.append(item->child(i));
    }
}

void NotesPage::applySearchResults(const QStringList& contentMatches, const QString& needle)
{
    auto visit = [&](auto&& self, QTreeWidgetItem* item) -> bool {
        const QString id = item->data(0, NodeIdRole).toString();
        bool match = id.isEmpty();
        if (m_nodes.contains(id)) {
            const NoteNode node = m_nodes.value(id);
            match = needle.isEmpty() || node.title.contains(needle, Qt::CaseInsensitive) || contentMatches.contains(id)
                || std::any_of(node.tags.cbegin(), node.tags.cend(), [&needle](const QString& tag) { return tag.contains(needle, Qt::CaseInsensitive); });
        }
        for (int i = 0; i < item->childCount(); ++i) match = self(self, item->child(i)) || match;
        item->setHidden(!match);
        return match;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) visit(visit, m_tree->topLevelItem(i));
}

void NotesPage::writeRecoveryJournal()
{
    if (!m_dirty || m_currentNoteId.isEmpty()) return;
    const QString path = journalPath(m_currentNoteId);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) { file.write(m_editor->toPlainText().toUtf8()); file.commit(); }
}

bool NotesPage::saveNow()
{
    if (!m_dirty || m_currentNoteId.isEmpty()) return true;
    const auto result = m_repository.saveContent(m_workspaceId, m_currentNoteId, m_editor->toPlainText(), m_currentContentHash);
    if (!result) { writeRecoveryJournal(); m_status->setText(result.error().userMessage); return false; }
    m_currentContentHash = result.value(); m_dirty = false; QFile::remove(journalPath(m_currentNoteId));
    if (m_nodes.contains(m_currentNoteId)) m_nodes[m_currentNoteId].contentHash = m_currentContentHash;
    m_status->setText(tr("Saved at %1").arg(QTime::currentTime().toString())); return true;
}

void NotesPage::updatePreview()
{
    QTextDocument* document = m_preview->document(); document->setMarkdown(m_editor->toPlainText());
    // QTextBrowser has no network access configured and never opens Markdown links automatically.
}

void NotesPage::showTextCompare(const QString& title, const QString& text)
{
    QDialog dialog(this); dialog.setWindowTitle(title); dialog.resize(760, 600);
    auto* layout = new QVBoxLayout(&dialog); auto* view = new QTextEdit(&dialog); view->setReadOnly(true); view->setPlainText(text);
    layout->addWidget(view); auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons); dialog.exec();
}

} // namespace devhub::presentation::notes
