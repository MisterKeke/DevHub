#include "CodexRuntimeProfile.h"
#include "application/assistant/CodexLoginPolicy.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QSet>

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
        "approval_policy=\"on-request\"", "forced_login_method=\"chatgpt\"", "model_provider=\"openai\""})
        arguments << "-c" << option;
    disabledMcpServers.removeDuplicates(); disabledMcpServers.sort();
    for (const auto& name : disabledMcpServers) {
        bool valid = !name.isEmpty() && name.size() <= 128;
        for (const auto ch : name) valid = valid && ch.unicode() >= 32 && ch.unicode() != 127;
        if (!valid) return Result<QStringList>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Configuration,
            QStringLiteral("An inherited MCP server name is invalid."), {}, QStringLiteral("codex.profile")));
        arguments << "-c" << "mcp_servers." + quoted(name) + ".enabled=false";
    }
    return Result<QStringList>::success(arguments);
}
bool CodexRuntimeProfile::acceptsLaunch(const QStringList& arguments)
{
    const auto base = launchArguments().value();
    if (arguments.size() < base.size() || (arguments.size() - base.size()) % 2 != 0) return false;
    for (qsizetype i = 0; i < base.size(); ++i) if (arguments.at(i) != base.at(i)) return false;
    QStringList servers;
    for (qsizetype i = base.size(); i < arguments.size(); i += 2) {
        const auto override = arguments.at(i + 1);
        const auto prefix = QStringLiteral("mcp_servers.");
        const auto suffix = QStringLiteral(".enabled=false");
        if (arguments.at(i) != "-c" || !override.startsWith(prefix) || !override.endsWith(suffix)) return false;
        const auto key = override.mid(prefix.size(), override.size() - prefix.size() - suffix.size());
        const auto parsed = QJsonDocument::fromJson(('[' + key + ']').toUtf8()).array();
        if (parsed.size() != 1 || !parsed.at(0).isString()) return false;
        servers.append(parsed.at(0).toString());
    }
    const auto canonical = launchArguments(servers);
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
    // Raw config layers are inspected transiently. Unknown/managed sources are
    // outside the bounded evidence and cannot inherit a development approval.
    const auto layers = configResult.value("layers");
    if (!layers.isArray()) reasons << "missing-config-layers";
    for (const auto& layer : layers.toArray()) {
        const auto source = layer.toObject().value("name").toObject().value("type").toString();
        if (source != "user" && source != "project" && source != "sessionFlags") reasons << "unverified-config-source";
    }
    const auto required = requirementsResult.value("requirements");
    if (!required.isUndefined() && !required.isNull()) {
        if (!required.isObject()) reasons << "invalid-managed-requirements";
        else {
            const auto requirements = required.toObject();
            // Reject every nonempty managed requirement until that exact policy
            // has controls. This includes hooks, feature conflicts and roots.
            for (auto it = requirements.begin(); it != requirements.end(); ++it)
                if (!it.value().isNull()) { reasons << "unverified-managed-requirements"; break; }
        }
    }
    for (const auto& key : {"browser_use", "computer_use", "desktop"}) {
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
    const auto profile = QJsonDocument(QJsonArray{"devhub-text-only-v1", "0.160.1", true,
        QJsonArray::fromStringList(arguments.isEmpty() ? launchArguments().value() : arguments), "environments-empty", "deny-server-tools", "legacy-history"});
    return QString::fromLatin1(QCryptographicHash::hash(profile.toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
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

bool CodexRuntimeProfile::developmentLiveInferenceAuthorized(
    const QString& executablePath, const QString& version, const QProcessEnvironment& childEnvironment)
{
    if (!developmentLiveValidationBuild() || version != QStringLiteral("0.160.1")
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
