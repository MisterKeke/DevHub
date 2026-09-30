#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace devhub::application::notes {

enum class NoteNodeKind { Folder, Note };

struct NoteNode final {
    QString id;
    QString workspaceId;
    QString parentId;
    QString title;
    NoteNodeKind kind = NoteNodeKind::Note;
    int sortOrder = 0;
    bool pinned = false;
    bool trashed = false;
    QString contentHash;
    QStringList tags;
};

} // namespace devhub::application::notes
