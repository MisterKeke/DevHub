#pragma once

#include <QString>
#include <QStringList>

namespace devhub::application::planner {

struct TaskNoteLink final {
    QString workspaceId;
    QString taskId;
    QString noteId;
    QString createdAtUtc;
};

struct LinkedNote final {
    QString noteId;
    QString title;
    QString folderPath;
    QStringList tags;
    bool trashed = false;
    bool alreadyLinked = false;
};

struct LinkedPlannerTask final {
    QString taskId;
    QString title;
    QString status;
    QString priority;
};

} // namespace devhub::application::planner
