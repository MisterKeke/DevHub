#pragma once

#include "FileBrowserController.h"
#include "../../application/files/ITrashService.h"
#include "../../application/files/IExternalEditorLauncher.h"

#include <QWidget>

class QFileSystemModel;
class QSortFilterProxyModel;
class QTreeView;
class QPlainTextEdit;
class QLabel;
class QCheckBox;
class QModelIndex;

namespace devhub::presentation::files {

class FileBrowserPage final : public QWidget
{
    Q_OBJECT
public:
    FileBrowserPage(application::files::ITrashService& trash,
                    application::files::IExternalEditorLauncher& editorLauncher,
                    QWidget* parent = nullptr);
    void setWorkspace(QString workspaceId, QString rootPath);
private:
    void updatePreview(const QModelIndex& proxyIndex);
    void showContextMenu(const QPoint& position);
    void createFile();
    void createDirectory();
    void renameSelected();
    void moveToRecycleBin();
    void openSelected();
    void openContainingFolder();
    void openWithEditor();
    void copySelected(bool relative);
    void expandAllBounded();
    void restoreExpansionState();
    void saveExpansionState();
    [[nodiscard]] QString selectedPath() const;
    [[nodiscard]] QString targetDirectory() const;
    void showError(const QString& title, const QString& message);

    FileBrowserController m_controller;
    application::files::ITrashService& m_trash;
    application::files::IExternalEditorLauncher& m_editorLauncher;
    QString m_workspaceId;
    QFileSystemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QTreeView* m_tree = nullptr;
    QPlainTextEdit* m_preview = nullptr;
    QLabel* m_status = nullptr;
    QCheckBox* m_showHidden = nullptr;
};

} // namespace devhub::presentation::files
