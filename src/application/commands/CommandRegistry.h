#pragma once

#include <QAction>
#include <QObject>
#include <QVector>
#include <QVariant>
#include <functional>

namespace devhub::application::commands {

enum class CommandCategory { Navigation, Account, Workspace, Git, Services, Ports, Logs, Database, Notes, Files, Settings };

struct CommandContext final {
    bool hasWorkspace = false;
    QString workspaceId;
    QString workspaceName;
    QString workspacePath;
};

struct CommandAvailability final {
    bool available = true;
    QString reason;
};

struct CommandDescriptor final {
    QString id;
    QString title;
    QString description;
    QStringList keywords;
    CommandCategory category = CommandCategory::Navigation;
    CommandAvailability availability;
    QAction* action = nullptr;
};

class CommandRegistry final : public QObject
{
    Q_OBJECT
public:
    using Availability = std::function<CommandAvailability(const CommandContext&)>;
    using Handler = std::function<void()>;
    using PayloadHandler = std::function<void(const QVariant&)>;
    explicit CommandRegistry(QObject* parent = nullptr);
    QAction* addCommand(QString id, QString title, CommandCategory category,
                        Handler handler, Availability availability = {},
                        QString description = {}, QStringList keywords = {});
    QAction* addPayloadCommand(QString id, QString title, CommandCategory category,
                        PayloadHandler handler, Availability availability = {},
                        QString description = {}, QStringList keywords = {});
    void setContext(CommandContext context);
    [[nodiscard]] CommandContext context() const { return m_context; }
    [[nodiscard]] QVector<CommandDescriptor> commands() const;
    [[nodiscard]] bool execute(const QString& id);
    [[nodiscard]] bool execute(const QString& id, const QVariant& payload);
    [[nodiscard]] bool contains(const QString& id) const;
    bool removeCommand(const QString& id);
    [[nodiscard]] QStringList recentCommandIds() const { return m_recent; }
    [[nodiscard]] CommandDescriptor descriptor(const QString& id) const;

signals:
    void commandsChanged();
    void contextChanged();
    void commandExecuted(QString id);

private:
    struct Entry final { CommandDescriptor descriptor; Availability availability; Handler handler; PayloadHandler payloadHandler; };
    void refreshAvailability();
    QVector<Entry> m_entries;
    CommandContext m_context;
    QStringList m_recent;
};

} // namespace devhub::application::commands
