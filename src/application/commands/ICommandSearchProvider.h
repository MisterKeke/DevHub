#pragma once

#include "CommandRegistry.h"

namespace devhub::application::commands {

struct SearchCandidate final {
    QString title;
    QString detail;
    QString commandId;
    QString provider;
};

class ICommandSearchProvider
{
public:
    virtual ~ICommandSearchProvider() = default;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual QVector<SearchCandidate> search(const QString& query,
                                                           const CommandContext& context) const = 0;
};

class CommandSearchProvider final : public ICommandSearchProvider
{
public:
    explicit CommandSearchProvider(const CommandRegistry& registry) : m_registry(registry) {}
    [[nodiscard]] QString name() const override { return QStringLiteral("Commands"); }
    [[nodiscard]] QVector<SearchCandidate> search(const QString& query,
                                                   const CommandContext& context) const override;
private:
    const CommandRegistry& m_registry;
};

class CachedSearchProvider : public ICommandSearchProvider
{
public:
    explicit CachedSearchProvider(QString providerName) : m_name(std::move(providerName)) {}
    [[nodiscard]] QString name() const override { return m_name; }
    void setEntries(QVector<SearchCandidate> entries) { m_entries = std::move(entries); }
    [[nodiscard]] QVector<SearchCandidate> search(const QString& query,
                                                   const CommandContext& context) const override;
private:
    QString m_name;
    QVector<SearchCandidate> m_entries;
};

class WorkspaceSearchProvider final : public CachedSearchProvider { public: WorkspaceSearchProvider() : CachedSearchProvider(QStringLiteral("Workspaces")) {} };
class ServiceSearchProvider final : public CachedSearchProvider { public: ServiceSearchProvider() : CachedSearchProvider(QStringLiteral("Services")) {} };
class NoteSearchProvider final : public CachedSearchProvider { public: NoteSearchProvider() : CachedSearchProvider(QStringLiteral("Notes")) {} };
class DatabaseSearchProvider final : public CachedSearchProvider { public: DatabaseSearchProvider() : CachedSearchProvider(QStringLiteral("Databases")) {} };
class SettingsSearchProvider final : public CachedSearchProvider { public: SettingsSearchProvider() : CachedSearchProvider(QStringLiteral("Settings")) {} };

} // namespace devhub::application::commands
