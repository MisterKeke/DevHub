#include "OverviewStateComposer.h"

namespace devhub::application::overview {

OverviewStateComposer::OverviewStateComposer(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<WorkspaceOverviewSnapshot>();
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &OverviewStateComposer::publish);
}

void OverviewStateComposer::setWorkspace(QString id, QString name, QString path)
{
    m_snapshot.workspaceId = std::move(id);
    m_snapshot.workspaceName = std::move(name);
    m_snapshot.workspacePath = std::move(path);
    m_debounce.start();
}

void OverviewStateComposer::updateSection(OverviewSection section)
{
    if (section.updatedAt.isNull()) section.updatedAt = QDateTime::currentDateTimeUtc();
    m_snapshot.sections.insert(section.id, std::move(section));
    m_debounce.start();
}

void OverviewStateComposer::removeSection(const QString& id)
{
    m_snapshot.sections.remove(id);
    m_debounce.start();
}

void OverviewStateComposer::publish()
{
    m_snapshot.composedAt = QDateTime::currentDateTimeUtc();
    emit snapshotChanged(m_snapshot);
}

} // namespace devhub::application::overview
