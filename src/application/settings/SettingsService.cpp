#include "SettingsService.h"
#include "../../domain/errors/AppError.h"

#include <QRegularExpression>

namespace devhub::application::settings {
namespace {
SettingDescriptor integer(QString id, QString label, QString section, int value, int min, int max,
                           SettingScope scope = SettingScope::Global, bool restart = false)
{
    return {std::move(id), std::move(label), std::move(section), value, scope, SettingSensitivity::Public, restart,
        [min, max](const QVariant& v) { bool ok = false; const int n = v.toInt(&ok); return ok && n >= min && n <= max; }};
}
SettingDescriptor boolean(QString id, QString label, QString section, bool value, SettingScope scope = SettingScope::Global, bool restart = false)
{
    return {std::move(id), std::move(label), std::move(section), value, scope, SettingSensitivity::Public, restart,
        [](const QVariant& v) { return v.metaType().id() == QMetaType::Bool; }};
}
SettingDescriptor string(QString id, QString label, QString section, QString value, bool restart = false,
                         SettingSensitivity sensitivity = SettingSensitivity::Public)
{
    return {std::move(id), std::move(label), std::move(section), std::move(value), SettingScope::Global, sensitivity, restart,
        [](const QVariant& v) { return v.metaType().id() == QMetaType::QString && v.toString().size() <= 2048; }};
}
}

SettingsService::SettingsService(ISettingsRepository& repository) : m_repository(repository)
{
    m_descriptors = {
        boolean(QStringLiteral("general/restoreLastPage"), QStringLiteral("Reopen the last page at startup"), QStringLiteral("General"), true),
        boolean(QStringLiteral("general/restoreWindowGeometry"), QStringLiteral("Restore window size and position"), QStringLiteral("General"), true),
        integer(QStringLiteral("security/inactivityTimeoutMinutes"), QStringLiteral("Inactivity lock timeout (minutes)"), QStringLiteral("Account and Security"), 15, 1, 240, SettingScope::Global, true),
        boolean(QStringLiteral("security/keepSessionDefault"), QStringLiteral("Keep session signed in by default"), QStringLiteral("Account and Security"), false, SettingScope::Global, true),
        boolean(QStringLiteral("appearance/reducedMotion"), QStringLiteral("Reduce motion"), QStringLiteral("Appearance"), false),
        string(QStringLiteral("appearance/theme"), QStringLiteral("Color theme"), QStringLiteral("Appearance"), QStringLiteral("dark-rose")),
        string(QStringLiteral("git/executable"), QStringLiteral("Git executable override"), QStringLiteral("Git"), {}, true, SettingSensitivity::Sensitive),
        integer(QStringLiteral("workspaces/scanLimitMs"), QStringLiteral("Workspace scan limit (ms)"), QStringLiteral("Workspaces"), 5000, 100, 60000, SettingScope::Global, true),
        integer(QStringLiteral("logs/maxLines"), QStringLiteral("Maximum cached log lines"), QStringLiteral("Logs"), 10000, 100, 200000, SettingScope::Global, true),
        integer(QStringLiteral("logs/maxBytes"), QStringLiteral("Maximum cached log bytes"), QStringLiteral("Logs"), 4194304, 65536, 67108864, SettingScope::Global, true),
        integer(QStringLiteral("ports/refreshIntervalSeconds"), QStringLiteral("Port refresh interval (seconds)"), QStringLiteral("Services"), 30, 5, 3600, SettingScope::Global, true),
        integer(QStringLiteral("database/pageSize"), QStringLiteral("Database page size"), QStringLiteral("Database"), 250, 25, 500, SettingScope::Global, true),
        boolean(QStringLiteral("database/queryHistoryEnabled"), QStringLiteral("Keep query history for this session"), QStringLiteral("Database"), false, SettingScope::Session),
        integer(QStringLiteral("database/queryHistoryLimit"), QStringLiteral("Query history limit"), QStringLiteral("Database"), 20, 1, 200),
        integer(QStringLiteral("notifications/retentionDays"), QStringLiteral("Notification retention (days)"), QStringLiteral("Notifications"), 90, 1, 3650, SettingScope::Global, true),
        string(QStringLiteral("terminal/profile"), QStringLiteral("Terminal profile"), QStringLiteral("Terminal"), QStringLiteral("windows-terminal")),
        string(QStringLiteral("terminal/customExecutable"), QStringLiteral("Custom terminal executable"), QStringLiteral("Terminal"), {}, false, SettingSensitivity::Sensitive),
        string(QStringLiteral("terminal/customArguments"), QStringLiteral("Custom terminal arguments"), QStringLiteral("Terminal"), {}, false, SettingSensitivity::Sensitive)
    };
    for (auto& item : m_descriptors) if (item.id == QStringLiteral("terminal/profile"))
        item.validate = [](const QVariant& value) {
            return QStringList{QStringLiteral("windows-terminal"), QStringLiteral("powershell"), QStringLiteral("cmd"),
                               QStringLiteral("wsl"), QStringLiteral("custom")}.contains(value.toString());
        };
    for (auto& item : m_descriptors) if (item.id == QStringLiteral("appearance/theme"))
        item.validate = [](const QVariant& value) {
            return value.metaType().id() == QMetaType::QString
                && QStringList{QStringLiteral("dark-rose"), QStringLiteral("dark-blue")}.contains(value.toString());
        };
    for (auto& item : m_descriptors) if (item.id == QStringLiteral("terminal/customArguments"))
        item.validate = [](const QVariant& value) {
            const QString text = value.toString();
            if (text.size() > 2048 || text.count(QLatin1Char('"')) % 2 != 0
                || text.count(QLatin1Char('\'')) % 2 != 0) return false;
            static const QRegularExpression placeholders(QStringLiteral("\\{([^}]*)\\}"));
            QString remaining = text;
            auto matches = placeholders.globalMatch(text);
            while (matches.hasNext()) {
                const auto match = matches.next();
                if (match.captured(1) != QStringLiteral("workspace")
                    && match.captured(1) != QStringLiteral("workspaceName")) return false;
                remaining.remove(match.capturedStart(), match.capturedLength());
            }
            return !remaining.contains(QLatin1Char('{')) && !remaining.contains(QLatin1Char('}'));
        };
    for (auto& item : m_descriptors) if (item.id == QStringLiteral("workspaces/scanLimitMs"))
        item.scope = SettingScope::Workspace;
}

SettingDescriptor SettingsService::descriptor(const QString& id) const
{
    for (const auto& item : m_descriptors) if (item.id == id) return item;
    return {};
}
QVector<SettingDescriptor> SettingsService::descriptors() const { return m_descriptors; }

SettingValues SettingsService::effectiveValues(const QString& workspaceId) const
{
    SettingValues values;
    for (const auto& item : m_descriptors) values.insert(item.id, item.defaultValue);
    const auto global = m_repository.load(SettingScope::Global);
    if (global) for (auto i = global.value().cbegin(); i != global.value().cend(); ++i) {
        const auto setting = descriptor(i.key());
        if (!setting.id.isEmpty() && setting.validate && setting.validate(i.value())) values.insert(i.key(), i.value());
    }
    if (!workspaceId.isEmpty()) {
        const auto workspace = m_repository.load(SettingScope::Workspace, workspaceId);
        if (workspace) for (auto i = workspace.value().cbegin(); i != workspace.value().cend(); ++i) {
            const auto setting = descriptor(i.key());
            if (!setting.id.isEmpty() && setting.validate && setting.validate(i.value())) values.insert(i.key(), i.value());
        }
    }
    for (auto i = m_sessionValues.cbegin(); i != m_sessionValues.cend(); ++i) values.insert(i.key(), i.value());
    return values;
}

QVariant SettingsService::value(const QString& id, const QString& workspaceId) const
{
    return effectiveValues(workspaceId).value(id, descriptor(id).defaultValue);
}

shared::common::Result<void> SettingsService::setValue(const QString& id, QVariant value, const QString& workspaceId)
{
    const auto item = descriptor(id);
    if (item.id.isEmpty() || !item.validate || !item.validate(value))
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Configuration,
            QStringLiteral("That setting value is invalid."), QStringLiteral("Setting value failed descriptor validation."),
            QStringLiteral("save application setting")));
    if (item.scope == SettingScope::Session) { m_sessionValues.insert(id, std::move(value)); return shared::common::Result<void>::success(); }
    const auto scope = item.scope == SettingScope::Workspace && !workspaceId.isEmpty()
        ? SettingScope::Workspace : SettingScope::Global;
    return m_repository.save(scope, scope == SettingScope::Workspace ? workspaceId : QString{}, id, value);
}

SettingValues SettingsService::sanitizedValues(const QString& workspaceId) const
{
    SettingValues output;
    const auto all = effectiveValues(workspaceId);
    for (const auto& item : m_descriptors)
        if (item.sensitivity == SettingSensitivity::Public && all.contains(item.id)) output.insert(item.label, all.value(item.id));
    return output;
}
} // namespace devhub::application::settings
