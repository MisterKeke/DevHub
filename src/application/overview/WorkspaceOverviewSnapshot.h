#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>

namespace devhub::application::overview {

struct OverviewSection final {
    QString id;
    QString title;
    QString summary;
    QString statusLabel;
    QString statusTone;
    QStringList details;
    QDateTime updatedAt;
    bool loading = false;
    QString error;
    QStringList actions;
};

struct WorkspaceOverviewSnapshot final {
    QString workspaceId;
    QString workspaceName;
    QString workspacePath;
    QDateTime composedAt;
    QMap<QString, OverviewSection> sections;
};

} // namespace devhub::application::overview
