#pragma once

#include "WorkspaceOverviewSnapshot.h"

#include <QObject>
#include <QTimer>

namespace devhub::application::overview {

// Combines already-available module state. It deliberately has no backend dependencies.
class OverviewStateComposer final : public QObject
{
    Q_OBJECT
public:
    explicit OverviewStateComposer(QObject* parent = nullptr);
    void setWorkspace(QString id, QString name, QString path);
    void updateSection(OverviewSection section);
    void removeSection(const QString& id);
    [[nodiscard]] WorkspaceOverviewSnapshot snapshot() const { return m_snapshot; }

signals:
    void snapshotChanged(devhub::application::overview::WorkspaceOverviewSnapshot snapshot);

private:
    void publish();
    WorkspaceOverviewSnapshot m_snapshot;
    QTimer m_debounce;
};

} // namespace devhub::application::overview

Q_DECLARE_METATYPE(devhub::application::overview::WorkspaceOverviewSnapshot)
