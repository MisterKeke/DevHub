#include "WorkspaceListView.h"
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>

namespace devhub::presentation::workspaces {
void WorkspaceListView::dragEnterEvent(QDragEnterEvent* event)
{ if (event->mimeData()->hasUrls()) event->acceptProposedAction(); else QListView::dragEnterEvent(event); }
void WorkspaceListView::dragMoveEvent(QDragMoveEvent* event)
{ if (event->mimeData()->hasUrls()) event->acceptProposedAction(); else QListView::dragMoveEvent(event); }
void WorkspaceListView::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        for (const auto& url : event->mimeData()->urls()) if (url.isLocalFile()) emit folderDropped(url.toLocalFile());
        event->acceptProposedAction(); return;
    }
    QListView::dropEvent(event);
}
}
