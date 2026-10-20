#include "CodexRuntimeProfile.h"
#include "CodexVersionSupport.h"
#include "CodexProtocolProfile.h"
#include "application/assistant/CodexLoginPolicy.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QSet>
#include <QHash>
#include <cmath>
#include <optional>

namespace devhub::infrastructure::codex {
namespace {
const QStringList& restrictedFeatures()
{
    static const QStringList names = {"shell_tool", "unified_exec", "apps", "plugins", "hooks",
        "browser_use", "computer_use", "goals", "image_generation", "multi_agent", "js_repl",
        "apply_patch_freeform", "shell_snapshot"};
    return names;
}
QString quoted(const QString& value)
{
    const auto encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}
bool disabled(const QJsonValue& value) { return value.isBool() && !value.toBool(); }
std::optional<QStringList> disabledServerNames(const QString& override)
{
    const QString prefix = QStringLiteral("mcp_servers={"), entry = QStringLiteral("={enabled=false}");
    if (!override.startsWith(prefix) || !override.endsWith('}')) return std::nullopt;
    QStringList names; qsizetype position = prefix.size(), end = override.size() - 1;
    while (position < end) {
        const auto start = position;
        if (override.at(position++) != '"') return std::nullopt;
        bool escaped = false, closed = false;
        while (position < end) {
            const auto ch = override.at(position++);
            if (!escaped && ch == '"') { closed = true; break; }
            if (!escaped && ch == '\\') escaped = true; else escaped = false;
        }
        if (!closed) return std::nullopt;
        const auto key = QJsonDocument::fromJson(('[' + override.mid(start, position - start) + ']').toUtf8()).array();
        if (key.size() != 1 || !key.first().isString() || override.mid(position, entry.size()) != entry) return std::nullopt;
        names.append(key.first().toString()); position += entry.size();
        if (position == end) break;
        if (position >= end || override.at(position++) != ',' || position == end) return std::nullopt;
    }
    return names.isEmpty() ? std::nullopt : std::optional<QStringList>{names};
}
bool desktopPreferencesOnly(const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull()) return true;
    if (!value.isObject()) return false;
    const auto preferences = value.toObject();
    for (auto it = preferences.begin(); it != preferences.end(); ++it) {
        if (it.key() == "ambient-suggestions-enabled" && disabled(it.value())) continue;
        if ((it.key() == "sansFontSize" || it.key() == "codeFontSize") && it.value().isDouble()
            && std::isfinite(it.value().toDouble()) && std::floor(it.value().toDouble()) == it.value().toDouble()
            && it.value().toDouble() > 0 && it.value().toDouble() <= 96) continue;
        if ((it.key() == "conversationDetailMode" || it.key() == "followUpQueueMode")
            && it.value().isString() && it.value().toString().size() <= 64) continue;
        return false;
    }
    return true;
}

QString canonicalPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).canonicalFilePath());
}

bool samePath(const QString& left, const QString& right)
{
    const auto canonicalLeft = QDir::fromNativeSeparators(canonicalPath(left)).toCaseFolded();
    const auto canonicalRight = QDir::fromNativeSeparators(canonicalPath(right)).toCaseFolded();
    return !canonicalLeft.isEmpty() && canonicalLeft == canonicalRight;
}

bool pathsOverlap(const QString& left, const QString& right)
{
    const auto canonicalLeft = QDir::fromNativeSeparators(canonicalPath(left)).toCaseFolded();
    const auto canonicalRight = QDir::fromNativeSeparators(canonicalPath(right)).toCaseFolded();
    return !canonicalLeft.isEmpty() && !canonicalRight.isEmpty()
        && (canonicalLeft == canonicalRight || canonicalLeft.startsWith(canonicalRight + '/')
            || canonicalRight.startsWith(canonicalLeft + '/'));
}

QString environmentValue(const QProcessEnvironment& environment, const QString& name)
{
    for (const auto& key : environment.keys()) {
        if (key.compare(name, Qt::CaseInsensitive) == 0) return environment.value(key);
    }
    return {};
}
}
shared::common::Result<QStringList> CodexRuntimeProfile::launchArguments(QStringList disabledMcpServers)
{
    using shared::common::Result;
    if (disabledMcpServers.size() > 64) return Result<QStringList>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Configuration,
        QStringLiteral("Too many inherited MCP servers for this profile."), {}, QStringLiteral("codex.profile")));
    QStringList arguments{"app-server", "--listen", "stdio://", "--strict-config"};
    for (const auto& feature : restrictedFeatures()) arguments << "-c" << "features." + feature + "=false";
    for (const auto& option : {"web_search=\"disabled\"", "sandbox_mode=\"read-only\"",
        "approval_policy=\"on-request\"", "forced_login_method=\"chatgpt\"", "model_provider=\"openai\"",
        "notify=[]", "desktop.ambient-suggestions-enabled=false"})
        arguments << "-c" << option;
    disabledMcpServers.removeDuplicates(); disabledMcpServers.sort();
    QStringList disabledEntries;
    for (const auto& name : disabledMcpServers) {
        bool valid = !name.isEmpty() && name.size() <= 128;
        for (const auto ch : name) valid = valid && ch.unicode() >= 32 && ch.unicode() != 127;
        if (!valid) return Result<QStringList>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Configuration,
            QStringLiteral("An inherited MCP server name is invalid."), {}, QStringLiteral("codex.profile")));
        disabledEntries.append(quoted(name) + QStringLiteral("={enabled=false}"));
    }
    // The pinned CLI splits dotted override paths literally; quoted segments
    // are not parsed as TOML keys. A table value preserves dots/quotes in names
    // and merges the disabled flag into each inherited server definition.
    if (!disabledEntries.isEmpty()) arguments << "-c" << "mcp_servers={" + disabledEntries.join(',') + '}';
    return Result<QStringList>::success(arguments);
}
bool CodexRuntimeProfile::acceptsLaunch(const QStringList& arguments)
{
    const auto base = launchArguments().value();
    if (arguments.size() != base.size() && arguments.size() != base.size() + 2) return false;
    for (qsizetype i = 0; i < base.size(); ++i) if (arguments.at(i) != base.at(i)) return false;
    if (arguments.size() == base.size()) return true;
    if (arguments.at(base.size()) != "-c") return false;
    const auto servers = disabledServerNames(arguments.last());
    if (!servers) return false;
    const auto canonical = launchArguments(*servers);
    return canonical && arguments == canonical.value();
}
QStringList CodexRuntimeProfile::rejectionReasons(const QJsonObject& configResult, const QJsonObject& requirementsResult)
{
    QStringList reasons;
    const auto configValue = configResult.value("config");
    if (!configValue.isObject()) return {QStringLiteral("invalid-config")};
    const auto config = configValue.toObject();
    if (!config.value("features").isObject()) reasons << "invalid-features";
    const auto features = config.value("features").toObject();
    for (const auto& name : restrictedFeatures()) if (!disabled(features.value(name))) reasons << "feature-not-disabled:" + name;
    if (config.value("web_search") != "disabled") reasons << "web-search-not-disabled";
    if (config.value("sandbox_mode") != "read-only" || config.value("approval_policy") != "on-request") reasons << "unsafe-default-policy";
    if (config.value("forced_login_method") != "chatgpt" || config.value("model_provider") != "openai") reasons << "unsupported-auth-or-provider";
    const auto providers = config.value("model_providers");
    if (!providers.isUndefined() && !providers.isNull() && (!providers.isObject() || !providers.toObject().isEmpty())) reasons << "unverified-provider-overrides";
    const auto notify = config.value("notify");
    if (!notify.isUndefined() && !notify.isNull() && (!notify.isArray() || !notify.toArray().isEmpty())) reasons << "legacy-notify-enabled";
    const auto servers = config.value("mcp_servers");
    if (!servers.isUndefined() && !servers.isObject()) reasons << "invalid-mcp-config";
    const auto serverObjects = servers.toObject();
    for (auto it = serverObjects.begin(); it != serverObjects.end(); ++it)
        if (!it.value().isObject() || !disabled(it.value().toObject().value("enabled"))) { reasons << "inherited-mcp-enabled"; break; }
    // Local file layers share the same effective-config checks above. Unknown
    // and enterprise/MDM sources remain outside the bounded profile.
    const auto layers = configResult.value("layers");
    if (!layers.isArray()) reasons << "missing-config-layers";
    if (layers.toArray().size() > 64) return {QStringLiteral("config-layer-limit")};
    for (const auto& layer : layers.toArray()) {
        const auto sourceObject = layer.toObject().value("name").toObject();
        const auto source = sourceObject.value("type").toString();
        if (source != "user" && source != "project" && source != "sessionFlags" && source != "system") reasons << "unverified-config-source";
        if ((source == "user" || source == "system") && !QFileInfo(sourceObject.value("file").toString()).isAbsolute()) reasons << "invalid-config-source-path";
        if (source == "project" && !QFileInfo(sourceObject.value("dotCodexFolder").toString()).isAbsolute()) reasons << "invalid-config-source-path";
    }
    const auto required = requirementsResult.value("requirements");
    if (!required.isUndefined() && !required.isNull()) {
        if (!required.isObject()) reasons << "invalid-managed-requirements";
        else {
            const auto requirements = required.toObject();
            // Login-method constraints are compatible only when they permit the
            // ChatGPT method we force and verify independently. Every other
            // non-null requirement still needs its own policy implementation.
            for (auto it = requirements.begin(); it != requirements.end(); ++it) {
                if (it.value().isNull()) continue;
                if (it.key() == QStringLiteral("allowedLoginMethods") && it.value().isArray()) {
                    const auto methods = it.value().toArray();
                    bool compatible = methods.contains(QJsonValue(QStringLiteral("chatgpt")));
                    for (const auto& method : methods)
                        compatible = compatible && (method == "chatgpt" || method == "api");
                    if (compatible) continue;
                }
                reasons << "unverified-managed-requirements"; break;
            }
        }
    }
    // Desktop appearance/queue preferences do not attach a browser or computer
    // runtime. Unknown desktop fields and active host capabilities stay blocked.
    if (!desktopPreferencesOnly(config.value("desktop"))) reasons << "unverified-attached-runtime";
    for (const auto& key : {"browser_use", "computer_use"}) {
        const auto attached = config.value(QLatin1String(key));
        if (!attached.isUndefined() && !attached.isNull() && (!attached.isObject() || !attached.toObject().isEmpty()))
            reasons << "unverified-attached-runtime";
    }
    const auto roots = config.value("runtime_workspace_roots");
    if (!roots.isUndefined() && !roots.isNull() && (!roots.isArray() || !roots.toArray().isEmpty())) reasons << "unverified-capability-roots";
    reasons.removeDuplicates(); return reasons;
}
QString CodexRuntimeProfile::fingerprint(const QStringList& arguments)
{
    return fingerprint(QStringLiteral("restricted-legacy-history-v2"), arguments);
}
QString CodexRuntimeProfile::fingerprint(const QString& adapterSemantics, const QStringList& arguments)
{
    const auto profile = QJsonDocument(QJsonArray{"devhub-text-only-v2", adapterSemantics, true,
        QJsonArray::fromStringList(arguments.isEmpty() ? launchArguments().value() : arguments),
        "filtered-process-environment-v1", "environments-empty", "deny-server-tools", "legacy-history"});
    return QString::fromLatin1(QCryptographicHash::hash(profile.toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QString CodexRuntimeProfile::legacyFingerprint(const QString& version, const QStringList& arguments, const QString& toolInventory)
{
    if (CodexProtocolProfile::fixture(version).id.isEmpty()) return {};
    const auto bytes = QJsonDocument(QJsonArray{"devhub-text-only-v1", version, true,
        QJsonArray::fromStringList(arguments.isEmpty() ? launchArguments().value() : arguments),
        "environments-empty", "deny-server-tools", "legacy-history"}).toJson(QJsonDocument::Compact);
    auto identity = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    if (!toolInventory.isEmpty()) identity = QCryptographicHash::hash(identity + '|' + toolInventory.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(identity);
}
QStringList CodexRuntimeProfile::inventoryRejectionReasons(const QString& method, const QJsonObject& result,
    const QJsonObject& configResult)
{
    const auto cursor = result.value("nextCursor");
    if (!cursor.isUndefined() && !cursor.isNull() && !cursor.isString()) return {"inventory-unverified"};
    if (!result.value("nextCursor").toString().isEmpty()) return {"inventory-incomplete"};
    if (method == "plugin/list") {
        if (!result.value("marketplaces").isArray() || !result.value("marketplaceLoadErrors").toArray().isEmpty()) return {"plugin-inventory-unverified"};
        for (const auto& value : result.value("marketplaces").toArray()) {
            const auto marketplace = value.toObject();
            if (!marketplace.value("plugins").isArray()) return {"plugin-inventory-unverified"};
            for (const auto& plugin : marketplace.value("plugins").toArray())
                if (!disabled(plugin.toObject().value("enabled"))) return {"plugin-enabled"};
        }
        return {};
    }
    if (!result.value("data").isArray()) return {"inventory-unverified"};
    const auto data = result.value("data").toArray();
    if (data.size() > 256) return {"inventory-incomplete"};
    if (method == "mcpServerStatus/list") {
        const auto configured = configResult.value("config").toObject().value("mcp_servers").toObject();
        QSet<QString> names;
        for (const auto& entry : data) {
            const auto row = entry.toObject();
            const auto name = row.value("name").toString();
            if (name.isEmpty() || names.contains(name) || !row.value("tools").isObject()
                || !row.value("resources").isArray() || !row.value("resourceTemplates").isArray())
                return {"mcp-inventory-unverified"};
            names.insert(name);
            const auto server = configured.value(name);
            if (!server.isObject() || !server.toObject().value("enabled").isBool())
                return {"mcp-inventory-unverified"};
            if (!disabled(server.toObject().value("enabled"))) return {"inherited-mcp-active"};
            // A process-wide inventory includes disabled configuration entries.
            // runtimeStatus is optional/nullable outside a thread; corroborate
            // those entries with this launch's effective config, never presence
            // alone. Explicit connection states must still say disabled.
            const auto status = row.value("runtimeStatus");
            if (!status.isUndefined() && !status.isNull() && status != "disabled")
                return {"inherited-mcp-active"};
            if (!row.value("tools").toObject().isEmpty() || !row.value("resources").toArray().isEmpty()
                || !row.value("resourceTemplates").toArray().isEmpty()) return {"inherited-mcp-active"};
            const auto toolsError = row.value("toolsError");
            if (!toolsError.isUndefined() && !toolsError.isNull()) return {"mcp-inventory-unverified"};
        }
        return {};
    }
    if (method == "hooks/list") {
        for (const auto& entry : data) {
            const auto row = entry.toObject();
            if (!row.value("hooks").isArray() || !row.value("errors").isArray() || !row.value("warnings").isArray()
                || !row.value("errors").toArray().isEmpty() || !row.value("warnings").toArray().isEmpty()) return {"hooks-inventory-unverified"};
            for (const auto& hook : row.value("hooks").toArray())
                if (!disabled(hook.toObject().value("enabled"))) return {"hook-enabled"};
        }
        return {};
    }
    if (method == "experimentalFeature/list") {
        QSet<QString> seen;
        QHash<QString, QJsonObject> features;
        for (const auto& entry : data) {
            const auto feature = entry.toObject(); const auto name = feature.value("name").toString();
            if (name.isEmpty() || seen.contains(name) || !feature.value("enabled").isBool()) return {"feature-inventory-unverified"};
            seen.insert(name);
            features.insert(name, feature);
        }
        // Effective config checks already require every restricted key. The
        // inventory must corroborate it rather than silently omit these flags.
        for (const auto& name : restrictedFeatures()) if (!seen.contains(name)) return {"feature-inventory-incomplete"};
        QStringList reasons;
        for (const auto& name : restrictedFeatures()) {
            const auto feature = features.value(name);
            if (disabled(feature.value("enabled"))) continue;
            // Modern stable unified_exec is an always-on backend selector; the
            // shell_tool master gate controls whether execution tools are offered.
            // Config still must contain our explicit overrides, and native Send
            // still needs fresh behavior evidence of an empty native inventory.
            if (name == "unified_exec" && feature.value("stage") == "stable"
                && disabled(features.value("shell_tool").value("enabled"))) continue;
            reasons << "native-feature-enabled:" + name;
        }
        return reasons;
    }
    return {"inventory-unverified"};
}
bool CodexRuntimeProfile::acceptsEnvironment(const QProcessEnvironment& environment)
{
    if (environment.inheritsFromParent()) return false;
    static const QSet<QString> allowed{"systemroot", "windir", "comspec", "path", "pathext", "userprofile", "homedrive",
        "homepath", "localappdata", "appdata", "temp", "tmp", "home", "lang", "lc_all", "codex_home"};
    for (const auto& key : environment.keys()) {
        if (!allowed.contains(key.toLower())) return false;
        if (key.compare("CODEX_HOME", Qt::CaseInsensitive) == 0) {
            const QFileInfo home(environment.value(key));
            if (!home.isAbsolute() || !home.isDir()) return false;
        }
    }
    return true;
}
bool CodexRuntimeProfile::developmentLiveValidationBuild()
{
#if defined(DEVHUB_ASSISTANT_LIVE_VALIDATION_BUILD) && DEVHUB_ASSISTANT_LIVE_VALIDATION_BUILD
    return true;
#else
    return false;
#endif
}
bool CodexRuntimeProfile::productionChatEnabled(const application::assistant::CodexCompatibilityReport& report)
{
    return !developmentLiveValidationBuild() && report.protocolCompatible && report.runtimePolicyVerified
        && report.state == application::assistant::CodexCompatibilityState::Compatible;
}

bool CodexRuntimeProfile::developmentLiveInferenceAuthorized(
    const QString& executablePath, const QString& version, const QProcessEnvironment& childEnvironment)
{
    if (!developmentLiveValidationBuild() || CodexProtocolProfile::fixture(version).id.isEmpty()
        || !acceptsEnvironment(childEnvironment)) return false;

    const auto processEnvironment = QProcessEnvironment::systemEnvironment();
    if (processEnvironment.value(QStringLiteral("DEVHUB_RUN_LIVE_ASSISTANT_TESTS"))
            != QStringLiteral("I_ACKNOWLEDGE_REAL_CODEX_INFERENCE")
        || processEnvironment.value(QStringLiteral("DEVHUB_LIVE_DISPOSABLE_PROFILE_ACK"))
            != QStringLiteral("I_CREATED_THIS_DISPOSABLE_PROFILE")) return false;

    const auto expectedExecutable = processEnvironment.value(QStringLiteral("DEVHUB_LIVE_CODEX_EXE"));
    const auto expectedHome = processEnvironment.value(QStringLiteral("DEVHUB_LIVE_CODEX_HOME"));
    const QFileInfo executableInfo(executablePath);
    const QFileInfo expectedExecutableInfo(expectedExecutable);
    const QFileInfo homeInfo(expectedHome);
    if (expectedExecutable.isEmpty() || expectedHome.isEmpty()
        || !executableInfo.isAbsolute() || !executableInfo.isFile() || !executableInfo.isExecutable()
        || !expectedExecutableInfo.isAbsolute() || !expectedExecutableInfo.isFile()
        || !expectedExecutableInfo.isExecutable() || !homeInfo.isAbsolute() || !homeInfo.isDir()
        || !samePath(executablePath, expectedExecutable)) return false;
#ifdef Q_OS_WIN
    if (executableInfo.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0) return false;
#endif

    const auto childHome = environmentValue(childEnvironment, QStringLiteral("CODEX_HOME"));
    if (childHome.isEmpty() || !samePath(expectedHome, childHome)) return false;
    const auto defaultHome = QDir::home().filePath(QStringLiteral(".codex"));
    if (QFileInfo(defaultHome).exists() && pathsOverlap(defaultHome, expectedHome)) return false;

    QFile marker(QDir(expectedHome).filePath(QStringLiteral(".devhub-disposable-codex-profile")));
    return marker.open(QIODevice::ReadOnly)
        && marker.read(128) == QByteArray("DevHub live integration disposable profile\n");
}

bool CodexRuntimeProfile::acceptsLoginUrl(const QUrl& url, bool deviceCode)
{
    return application::assistant::acceptsCodexLoginUrl(url, deviceCode);
}
}
