#pragma once

#include <QMap>
#include <QVariant>
#include <functional>

namespace devhub::application::settings {
enum class SettingScope { Global, Workspace, Session };
enum class SettingSensitivity { Public, Sensitive };
struct SettingDescriptor final {
    QString id;
    QString label;
    QString section;
    QVariant defaultValue;
    SettingScope scope = SettingScope::Global;
    SettingSensitivity sensitivity = SettingSensitivity::Public;
    bool requiresRestart = false;
    std::function<bool(const QVariant&)> validate;
};
using SettingValues = QMap<QString, QVariant>;
struct SettingUpdate final {
    SettingScope scope = SettingScope::Global;
    QString workspaceId;
    QString id;
    QVariant value;
};
using SettingUpdates = QVector<SettingUpdate>;
} // namespace devhub::application::settings
