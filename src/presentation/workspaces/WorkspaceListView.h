#pragma once

#include <QListView>

namespace devhub::presentation::workspaces {
class WorkspaceListView final : public QListView
{
    Q_OBJECT
public:
    using QListView::QListView;
signals:
    void folderDropped(QString path);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};
}
