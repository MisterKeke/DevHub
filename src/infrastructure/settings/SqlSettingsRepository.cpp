#include "SqlSettingsRepository.h"
#include "../database/SqlErrorMapper.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSqlQuery>

namespace devhub::infrastructure::settings {
using application::settings::SettingScope;
using application::settings::SettingValues;

shared::common::Result<SettingValues> SqlSettingsRepository::load(SettingScope scope, const QString& workspaceId)
{
    if (scope == SettingScope::Session)
        return shared::common::Result<SettingValues>::success({});
    QSqlQuery query(m_manager.database());
    query.prepare(QStringLiteral("SELECT setting_key,value_json FROM app_settings WHERE scope=? AND workspace_id=?"));
    query.addBindValue(scope == SettingScope::Global ? QStringLiteral("global") : QStringLiteral("workspace"));
    query.addBindValue(scope == SettingScope::Global ? QStringLiteral("") : workspaceId);
    if (!query.exec()) return shared::common::Result<SettingValues>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("load application settings")));
    SettingValues values;
    while (query.next()) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(query.value(1).toByteArray(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue;
        values.insert(query.value(0).toString(), document.object().value(QStringLiteral("value")).toVariant());
    }
    return shared::common::Result<SettingValues>::success(std::move(values));
}

shared::common::Result<void> SqlSettingsRepository::save(SettingScope scope, const QString& workspaceId,
                                                          const QString& id, const QVariant& value)
{
    if (scope == SettingScope::Session)
        return shared::common::Result<void>::success();
    QSqlQuery query(m_manager.database());
    query.prepare(QStringLiteral("INSERT INTO app_settings(scope,workspace_id,setting_key,value_json,updated_at_utc) "
        "VALUES(?,?,?,?,?) ON CONFLICT(scope,workspace_id,setting_key) DO UPDATE SET "
        "value_json=excluded.value_json,updated_at_utc=excluded.updated_at_utc"));
    query.addBindValue(scope == SettingScope::Global ? QStringLiteral("global") : QStringLiteral("workspace"));
    query.addBindValue(scope == SettingScope::Global ? QStringLiteral("") : workspaceId);
    query.addBindValue(id);
    query.addBindValue(QJsonDocument(QJsonObject{{QStringLiteral("value"), QJsonValue::fromVariant(value)}})
                           .toJson(QJsonDocument::Compact));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) return shared::common::Result<void>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), QStringLiteral("save application setting")));
    return shared::common::Result<void>::success();
}
} // namespace devhub::infrastructure::settings
