#include "CommandRegistry.h"

#include <algorithm>

namespace devhub::application::commands {

CommandRegistry::CommandRegistry(QObject* parent) : QObject(parent) {}

QAction* CommandRegistry::addCommand(QString id, QString title, CommandCategory category,
                                     Handler handler, Availability availability,
                                     QString description, QStringList keywords)
{
    auto* action = new QAction(title, this);
    Entry entry;
    entry.descriptor = {std::move(id), std::move(title), std::move(description), std::move(keywords),
                        category, {}, action};
    entry.availability = std::move(availability);
    entry.handler = std::move(handler);
    connect(action, &QAction::triggered, this, [this, commandId = entry.descriptor.id] { (void)execute(commandId); });
    m_entries.push_back(std::move(entry));
    refreshAvailability();
    emit commandsChanged();
    return action;
}

void CommandRegistry::setContext(CommandContext context)
{
    m_context = std::move(context);
    refreshAvailability();
    emit contextChanged();
    emit commandsChanged();
}

void CommandRegistry::refreshAvailability()
{
    for (auto& entry : m_entries) {
        entry.descriptor.availability = entry.availability ? entry.availability(m_context) : CommandAvailability{};
        if (entry.descriptor.action) {
            entry.descriptor.action->setEnabled(entry.descriptor.availability.available);
            entry.descriptor.action->setStatusTip(entry.descriptor.availability.reason);
        }
    }
}

QVector<CommandDescriptor> CommandRegistry::commands() const
{
    QVector<CommandDescriptor> result;
    result.reserve(m_entries.size());
    for (const auto& entry : m_entries) result.push_back(entry.descriptor);
    return result;
}

CommandDescriptor CommandRegistry::descriptor(const QString& id) const
{
    for (const auto& entry : m_entries) if (entry.descriptor.id == id) return entry.descriptor;
    return {};
}

bool CommandRegistry::execute(const QString& id)
{
    for (auto& entry : m_entries) {
        if (entry.descriptor.id != id) continue;
        if (!entry.descriptor.availability.available || !entry.handler) return false;
        entry.handler();
        m_recent.removeAll(id);
        m_recent.prepend(id);
        while (m_recent.size() > 30) m_recent.removeLast();
        emit commandExecuted(id);
        return true;
    }
    return false;
}

bool CommandRegistry::contains(const QString& id) const
{
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [&id](const Entry& entry) {
        return entry.descriptor.id == id;
    });
}

bool CommandRegistry::removeCommand(const QString& id)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&id](const Entry& entry) {
        return entry.descriptor.id == id;
    });
    if (it == m_entries.end()) return false;
    if (it->descriptor.action) it->descriptor.action->deleteLater();
    m_entries.erase(it);
    m_recent.removeAll(id);
    emit commandsChanged();
    return true;
}

} // namespace devhub::application::commands
