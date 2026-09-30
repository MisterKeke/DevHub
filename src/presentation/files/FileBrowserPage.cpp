#include "FileBrowserPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStyle>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTextStream>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace devhub::presentation::files {
namespace {
class HiddenFilesProxy final : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool showHidden = false;
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        const auto* fs = qobject_cast<const QFileSystemModel*>(sourceModel());
        if (!fs) return true;
        const QModelIndex index = fs->index(sourceRow, 0, sourceParent);
        const QString name = fs->fileName(index);
        const QFileInfo info(fs->filePath(index));
        return showHidden || (!name.startsWith(QLatin1Char('.')) && !info.isHidden());
    }
};
constexpr qint64 PreviewLimit = 1024 * 1024;
}

FileBrowserPage::FileBrowserPage(application::files::ITrashService& trash,
                                 application::files::IExternalEditorLauncher& editorLauncher,
                                 QWidget* parent)
    : QWidget(parent), m_trash(trash), m_editorLauncher(editorLauncher)
{
    auto* root = new QVBoxLayout(this); root->setContentsMargins(24, 20, 24, 20);
    auto* toolbar = new QHBoxLayout();
    auto* expand = new QPushButton(tr("Expand"), this); auto* collapse = new QPushButton(tr("Collapse"), this);
    m_showHidden = new QCheckBox(tr("Show hidden files"), this);
    toolbar->addWidget(expand); toolbar->addWidget(collapse); toolbar->addWidget(m_showHidden); toolbar->addStretch(); root->addLayout(toolbar);
    auto* split = new QSplitter(Qt::Horizontal, this);
    m_model = new QFileSystemModel(this); m_model->setReadOnly(true);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_proxy = new HiddenFilesProxy(this); m_proxy->setSourceModel(m_model);
    m_tree = new QTreeView(split); m_tree->setObjectName(QStringLiteral("workspaceFileTree")); m_tree->setModel(m_proxy);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu); m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSortingEnabled(true); m_tree->sortByColumn(0, Qt::AscendingOrder); m_tree->setMinimumWidth(320);
    m_preview = new QPlainTextEdit(split); m_preview->setObjectName(QStringLiteral("filePreview"));
    m_preview->setReadOnly(true); m_preview->setPlaceholderText(tr("Select a small UTF-8 or ASCII text file to preview."));
    split->addWidget(m_tree); split->addWidget(m_preview); split->setStretchFactor(1, 1); root->addWidget(split, 1);
    m_status = new QLabel(tr("Choose a workspace to browse its files."), this); m_status->setWordWrap(true); root->addWidget(m_status);
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex& index) { updatePreview(index); });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        m_tree->setCurrentIndex(index);
        if (!m_model->isDir(m_proxy->mapToSource(index))) openSelected();
    });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &FileBrowserPage::showContextMenu);
    connect(m_showHidden, &QCheckBox::toggled, this, [this](bool enabled) { static_cast<HiddenFilesProxy*>(m_proxy)->showHidden = enabled; m_proxy->invalidate(); });
    m_showHidden->setChecked(QSettings().value(QStringLiteral("files/showHidden"), false).toBool());
    connect(m_showHidden, &QCheckBox::toggled, this, [](bool enabled) { QSettings().setValue(QStringLiteral("files/showHidden"), enabled); });
    connect(expand, &QPushButton::clicked, this, &FileBrowserPage::expandAllBounded);
    connect(collapse, &QPushButton::clicked, this, [this] { m_tree->collapseAll(); saveExpansionState(); });
    connect(m_tree, &QTreeView::expanded, this, [this] { saveExpansionState(); });
    connect(m_tree, &QTreeView::collapsed, this, [this] { saveExpansionState(); });
}

void FileBrowserPage::setWorkspace(QString workspaceId, QString rootPath)
{
    saveExpansionState(); m_workspaceId = std::move(workspaceId); m_controller.setWorkspaceRoot(rootPath);
    if (rootPath.isEmpty() || !QFileInfo(rootPath).isDir()) { m_status->setText(tr("The active workspace folder is unavailable.")); m_tree->setRootIndex({}); return; }
    const QModelIndex sourceRoot = m_model->setRootPath(QFileInfo(rootPath).canonicalFilePath());
    m_tree->setRootIndex(m_proxy->mapFromSource(sourceRoot));
    m_tree->setColumnWidth(0, 300); restoreExpansionState();
    m_status->setText(tr("Workspace root: %1. Preview limit: 1 MiB.").arg(rootPath));
}

QString FileBrowserPage::selectedPath() const
{
    const QModelIndex index = m_proxy->mapToSource(m_tree->currentIndex()); return m_model->filePath(index);
}

QString FileBrowserPage::targetDirectory() const
{
    const QString path = selectedPath(); if (path.isEmpty()) return m_controller.workspaceRoot();
    const QFileInfo info(path); return info.isDir() ? path : info.absolutePath();
}

void FileBrowserPage::updatePreview(const QModelIndex& proxyIndex)
{
    m_preview->clear();
    const QString path = m_model->filePath(m_proxy->mapToSource(proxyIndex));
    const QFileInfo info(path);
    if (path.isEmpty()) { m_status->setText(tr("Select a file to preview it.")); return; }
    if ((info.isSymLink() || info.isJunction()) && !m_controller.isInsideWorkspace(path, true)) {
        m_preview->setPlainText(tr("This symbolic link or junction points outside the active workspace. Preview and workspace actions are blocked."));
        m_status->setText(tr("External link target: %1").arg(info.symLinkTarget())); return;
    }
    if (info.isDir()) { m_status->setText(tr("Select a file to preview it.")); return; }
    if (!m_controller.isInsideWorkspace(path, false)) { m_status->setText(tr("This path is outside the workspace.")); return; }
    if (!m_controller.isInsideWorkspace(path, true)) {
        m_preview->setPlainText(tr("The resolved target is outside the workspace. Preview is blocked."));
        m_status->setText(tr("Linked target leaves the workspace")); return;
    }
    if (info.size() > PreviewLimit) { m_preview->setPlainText(tr("Preview unavailable: file is larger than 1 MiB.")); m_status->setText(tr("%1 bytes").arg(info.size())); return; }
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { m_status->setText(tr("Could not read this file: %1").arg(file.errorString())); return; }
    QByteArray bytes = file.readAll();
    if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) bytes.remove(0, 3);
    if (bytes.contains('\0')) { m_preview->setPlainText(tr("Preview unavailable: this looks like a binary file.")); m_status->setText(tr("Binary file (%1 bytes)").arg(info.size())); return; }
    QString text = QString::fromUtf8(bytes.constData(), bytes.size());
    if (text.toUtf8() != bytes) { m_preview->setPlainText(tr("Preview unavailable: this file is not UTF-8 or ASCII.")); m_status->setText(tr("Unsupported text encoding")); return; }
    m_preview->setPlainText(text); m_status->setText(tr("%1  ·  %2 bytes").arg(m_controller.relativePath(path)).arg(info.size()));
}

void FileBrowserPage::showContextMenu(const QPoint& position)
{
    QMenu menu(this);
    QAction* open = menu.addAction(tr("Open with system application"));
    QAction* editor = menu.addAction(tr("Open with configured editor…"));
    QAction* folder = menu.addAction(tr("Open Containing Folder"));
    menu.addSeparator();
    QAction* copyAbsolute = menu.addAction(tr("Copy Absolute Path"));
    QAction* copyRelative = menu.addAction(tr("Copy Workspace-Relative Path"));
    menu.addSeparator();
    QAction* createFileAction = menu.addAction(tr("Create File…")); QAction* createFolder = menu.addAction(tr("Create Directory…"));
    QAction* rename = menu.addAction(tr("Rename…")); QAction* recycle = menu.addAction(tr("Move to Recycle Bin…"));
    QAction* selected = menu.exec(m_tree->viewport()->mapToGlobal(position));
    if (selected == open) openSelected(); else if (selected == editor) openWithEditor(); else if (selected == folder) openContainingFolder();
    else if (selected == copyAbsolute) copySelected(false); else if (selected == copyRelative) copySelected(true);
    else if (selected == createFileAction) createFile(); else if (selected == createFolder) createDirectory();
    else if (selected == rename) renameSelected(); else if (selected == recycle) moveToRecycleBin();
}

void FileBrowserPage::createFile()
{
    bool accepted = false; const QString name = QInputDialog::getText(this, tr("Create File"), tr("Filename"), QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    QString error;
    if (!m_controller.validateWindowsName(name, &error)) { showError(tr("Invalid filename"), error); return; }
    const QString path = QDir(targetDirectory()).filePath(name);
    if (QFileInfo::exists(path)) { showError(tr("Name already exists"), tr("Choose a different name.")); return; }
    const QString parent = targetDirectory();
    if (!QFileInfo(parent).isDir() || !m_controller.isInsideWorkspace(parent, true)) { showError(tr("Outside workspace"), tr("The new file must stay inside the workspace.")); return; }
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || !file.commit()) { showError(tr("Could not create file"), file.errorString()); return; }
}

void FileBrowserPage::createDirectory()
{
    bool accepted = false; const QString name = QInputDialog::getText(this, tr("Create Directory"), tr("Folder name"), QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    QString error;
    if (!m_controller.validateWindowsName(name, &error)) { showError(tr("Invalid folder name"), error); return; }
    const QString path = QDir(targetDirectory()).filePath(name);
    if (QFileInfo::exists(path)) { showError(tr("Name already exists"), tr("Choose a different name.")); return; }
    const QString parent = targetDirectory();
    if (!QFileInfo(parent).isDir() || !m_controller.isInsideWorkspace(parent, true) || !QDir(parent).mkdir(name)) { showError(tr("Could not create directory"), tr("The directory could not be created inside this workspace.")); return; }
}

void FileBrowserPage::renameSelected()
{
    const QString path = selectedPath(); if (path.isEmpty()) return; QString error;
    if (!m_controller.validateMutationTarget(path, &error)) { showError(tr("Rename blocked"), error); return; }
    const QFileInfo info(path); bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name"), QLineEdit::Normal, info.fileName(), &accepted);
    if (!accepted || name == info.fileName()) return;
    if (!m_controller.validateWindowsName(name, &error)) { showError(tr("Invalid name"), error); return; }
    if (!m_controller.validateMutationTarget(path, &error)) { showError(tr("Rename blocked"), error); return; }
    const QFileInfo current(path);
    const QString target = QDir(current.absolutePath()).filePath(name);
    if (QFileInfo::exists(target)) { showError(tr("Name already exists"), tr("Choose a different name.")); return; }
    const bool renamed = current.isDir() ? QDir().rename(path, target) : QFile::rename(path, target);
    if (!renamed) showError(tr("Rename failed"), tr("The file or folder could not be renamed."));
    else { /* QFileSystemModel observes this directory through its watcher. */ }
}

void FileBrowserPage::moveToRecycleBin()
{
    const QString path = selectedPath(); if (path.isEmpty()) return; QString error;
    if (!m_controller.validateMutationTarget(path, &error)) { showError(tr("Delete blocked"), error); return; }
    const QFileInfo info(path);
    if ((info.isSymLink() || info.isJunction()) && !m_controller.isInsideWorkspace(path, true)) { showError(tr("Delete blocked"), tr("This link points outside the workspace.")); return; }
    if (QMessageBox::warning(this, tr("Move to Recycle Bin"), tr("Move “%1” to the Windows Recycle Bin?").arg(info.fileName()), QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (!m_controller.validateMutationTarget(path, &error)) { showError(tr("Delete blocked"), error); return; }
    const auto result = m_trash.moveToTrash(path);
    if (!result) { showError(tr("Recycle Bin failed"), result.error().userMessage); return; }
}

void FileBrowserPage::openSelected()
{
    const QString path = selectedPath(); if (path.isEmpty()) return;
    if (!m_controller.isInsideWorkspace(path, true)) { showError(tr("Open blocked"), tr("The resolved file or folder is outside the workspace.")); return; }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) showError(tr("Could not open file"), tr("No system-associated application opened this path."));
}

void FileBrowserPage::openContainingFolder()
{
    const QString path = selectedPath(); if (path.isEmpty()) return;
    const QString folder = QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath();
    if (!m_controller.isInsideWorkspace(folder, true)) { showError(tr("Open blocked"), tr("The containing folder resolves outside the workspace.")); return; }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) showError(tr("Could not open folder"), folder);
}

void FileBrowserPage::openWithEditor()
{
    const QString path = selectedPath(); if (path.isEmpty() || QFileInfo(path).isDir()) return;
    if (!m_controller.isInsideWorkspace(path, true)) { showError(tr("Editor blocked"), tr("The resolved file is outside the workspace.")); return; }
    QString program = QSettings().value(QStringLiteral("files/editorProgram")).toString();
    if (program.isEmpty() || !QFileInfo(program).isFile()) program = QFileDialog::getOpenFileName(this, tr("Choose IDE or editor executable"), {}, tr("Applications (*.exe);;All files (*)"));
    if (program.isEmpty()) return;
    QSettings().setValue(QStringLiteral("files/editorProgram"), program);
    const auto launched = m_editorLauncher.launch(program, path);
    if (!launched) showError(tr("Could not start editor"), launched.error().userMessage);
}

void FileBrowserPage::copySelected(bool relative)
{
    const QString path = selectedPath(); if (path.isEmpty()) return;
    QApplication::clipboard()->setText(relative ? m_controller.relativePath(path) : QDir::toNativeSeparators(path));
    m_status->setText(relative ? tr("Copied workspace-relative path.") : tr("Copied absolute path."));
}

void FileBrowserPage::expandAllBounded()
{
    QVector<QModelIndex> pending{m_tree->rootIndex()}; int expanded = 0;
    while (!pending.isEmpty() && expanded < 100) {
        const QModelIndex parent = pending.takeLast(); m_tree->setExpanded(parent, true); ++expanded;
        const int count = m_proxy->rowCount(parent);
        for (int row = 0; row < count && pending.size() < 100; ++row) {
            const QModelIndex child = m_proxy->index(row, 0, parent);
            if (m_model->isDir(m_proxy->mapToSource(child))) pending.append(child);
        }
    }
    m_status->setText(tr("Expanded up to 100 folders to keep browsing bounded.")); saveExpansionState();
}

void FileBrowserPage::restoreExpansionState()
{
    const QStringList paths = QSettings().value(QStringLiteral("files/expanded/%1").arg(m_workspaceId)).toStringList();
    for (const auto& path : paths.mid(0, 100)) {
        const QModelIndex source = m_model->index(path); if (source.isValid()) m_tree->setExpanded(m_proxy->mapFromSource(source), true);
    }
}

void FileBrowserPage::saveExpansionState()
{
    if (m_workspaceId.isEmpty() || !m_proxy) return;
    QStringList expanded; QVector<QModelIndex> pending{m_tree->rootIndex()};
    while (!pending.isEmpty() && expanded.size() < 100) {
        const QModelIndex parent = pending.takeLast(); if (m_tree->isExpanded(parent)) expanded.append(m_model->filePath(m_proxy->mapToSource(parent)));
        for (int row = 0; row < m_proxy->rowCount(parent) && pending.size() < 100; ++row) pending.append(m_proxy->index(row, 0, parent));
    }
    QSettings().setValue(QStringLiteral("files/expanded/%1").arg(m_workspaceId), expanded);
}

void FileBrowserPage::showError(const QString& title, const QString& message) { QMessageBox::warning(this, title, message); }

} // namespace devhub::presentation::files
