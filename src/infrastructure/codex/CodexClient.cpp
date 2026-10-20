#include "CodexClient.h"
#include "CodexRuntimeProfile.h"
#include "CodexVersionSupport.h"
#include "CodexSchemaInspector.h"
#include "CodexSchemaSurface.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace devhub::infrastructure::codex {
using namespace application::assistant;
using domain::errors::AppError;
using domain::errors::ErrorCode;
using shared::common::Result;
namespace {
AppError error(ErrorCode code, const QString& message)
{
    return AppError::failure(code, domain::errors::ErrorCategory::Process, message, {}, QStringLiteral("codex.client"));
}
AppError denied() { return error(ErrorCode::AssistantPermissionDenied, QStringLiteral("The Assistant operation is not admitted in this scope.")); }
AppError invalidResponse() { return error(ErrorCode::CodexProtocolFailure, QStringLiteral("Codex returned an incompatible response.")); }
QJsonObject dynamicToolFailure(const QString& code)
{
    const QJsonObject body{{QStringLiteral("success"), false},
        {QStringLiteral("data"), QJsonObject{{QStringLiteral("error"), code}}}};
    return {{QStringLiteral("success"), false},
        {QStringLiteral("contentItems"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("inputText")},
            {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact))}}}}};
}
QByteArray idKey(const QJsonValue& id) { return QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact); }
bool stringId(const QString& id) { return !id.isEmpty() && id.size() <= 512; }
bool terminal(CodexTurnStatus status) { return status != CodexTurnStatus::InProgress; }
bool safeTextItem(const QString& type) { return type == "agentMessage" || type == "userMessage" || type == "reasoning" || type == "plan" || type == "contextCompaction"; }
QString behaviorEvidenceKey(const CodexCompatibilityReport& report)
{
    return CodexBehaviorVerifier::evidenceIdentity(report);
}
}
QString CodexClient::method(Operation operation)
{
    switch (operation) {
    case Operation::Initialize: return QStringLiteral("initialize");
    case Operation::Account: return QStringLiteral("account/read");
    case Operation::LoginStart: return QStringLiteral("account/login/start");
    case Operation::LoginCancel: return QStringLiteral("account/login/cancel");
    case Operation::Models: return QStringLiteral("model/list");
    case Operation::Config: return QStringLiteral("config/read");
    case Operation::Requirements: return QStringLiteral("configRequirements/read");
    case Operation::Features: return QStringLiteral("experimentalFeature/list");
    case Operation::Hooks: return QStringLiteral("hooks/list");
    case Operation::Mcp: return QStringLiteral("mcpServerStatus/list");
    case Operation::Plugins: return QStringLiteral("plugin/list");
    case Operation::ThreadStart: return QStringLiteral("thread/start");
    case Operation::ThreadResume: return QStringLiteral("thread/resume");
    case Operation::ThreadRead: return QStringLiteral("thread/read");
    case Operation::TurnStart: return QStringLiteral("turn/start");
    case Operation::TurnInterrupt: return QStringLiteral("turn/interrupt");
    }
    return {};
}
QString CodexClient::responseContract(Operation operation)
{
    switch (operation) {
    case Operation::Initialize: return QStringLiteral("Response:InitializeResponse");
    case Operation::Account: return QStringLiteral("Response:GetAccountResponse");
    case Operation::LoginStart: return QStringLiteral("Response:LoginAccountResponse");
    case Operation::LoginCancel: return QStringLiteral("Response:CancelLoginAccountResponse");
    case Operation::Models: return QStringLiteral("Response:ModelListResponse");
    case Operation::Config: return QStringLiteral("Response:ConfigReadResponse");
    case Operation::Requirements: return QStringLiteral("Response:ConfigRequirementsReadResponse");
    case Operation::Features: return QStringLiteral("Response:ExperimentalFeatureListResponse");
    case Operation::Hooks: return QStringLiteral("Response:HooksListResponse");
    case Operation::Mcp: return QStringLiteral("Response:ListMcpServerStatusResponse");
    case Operation::Plugins: return QStringLiteral("Response:PluginListResponse");
    case Operation::ThreadStart: return QStringLiteral("Response:ThreadStartResponse");
    case Operation::ThreadResume: return QStringLiteral("Response:ThreadResumeResponse");
    case Operation::ThreadRead: return QStringLiteral("Response:ThreadReadResponse");
    case Operation::TurnStart: return QStringLiteral("Response:TurnStartResponse");
    case Operation::TurnInterrupt: return QStringLiteral("Response:TurnInterruptResponse");
    }
    return {};
}
CodexClient::CodexClient(ICodexTransport& transport, QObject* parent, CodexClientLimits limits)
    : ICodexClient(parent), m_transport(&transport), m_limits(limits)
{
    m_clock.start(); m_deadlineTimer.setInterval(10); m_loginTimer.setSingleShot(true);
    connect(&m_executableWatcher, &QFileSystemWatcher::fileChanged, this, [this] {
        if (!m_active || m_compatibility.binaryIdentity.isEmpty()) return;
        invalidateCompatibility(CodexCompatibilityReason::ExecutableChanged,
            "The selected executable changed. Reconnect to inspect and verify its replacement.");
    });
    const auto configurationChanged = [this](const QString& path) {
        if (m_active && (m_configurationPaths.contains(path) || configurationStamp() != m_configurationStamp))
            invalidateCompatibility(CodexCompatibilityReason::ConfigurationChanged,
                "Codex configuration changed. Reconnect to verify the effective restrictions.");
    };
    connect(&m_configurationWatcher, &QFileSystemWatcher::fileChanged, this, configurationChanged);
    connect(&m_configurationWatcher, &QFileSystemWatcher::directoryChanged, this, configurationChanged);
    connect(&m_behaviorVerifier, &CodexBehaviorVerifier::completed, this, [this](bool accepted, const QString& reason) {
        if (m_behaviorVerificationGeneration != m_generation) return;
        m_compatibility.verificationInProgress = false;
        if (!m_active || !m_initialized || !m_profileVerified) return;
        if (configurationStamp() != m_configurationStamp) {
            invalidateCompatibility(CodexCompatibilityReason::ConfigurationChanged,
                "Codex configuration changed during verification. Reconnect before sending."); return;
        }
        m_compatibility.runtimePolicyVerified = accepted;
        m_compatibility.state = accepted ? CodexCompatibilityState::Compatible : CodexCompatibilityState::VerificationRequired;
        m_compatibility.reason = accepted ? CodexCompatibilityReason::None : CodexCompatibilityReason::RuntimeVerificationRequired;
        m_compatibility.explanation = reason;
        if (accepted) m_compatibility.evidence << "fresh-isolated-behavior-v1";
        if (accepted) {
            if (m_sessionBehaviorEvidence.size() >= 8) m_sessionBehaviorEvidence.clear();
            m_sessionBehaviorEvidence.insert(behaviorEvidenceKey(m_compatibility));
            if (m_transport && m_transport->kind() == CodexTransportKind::NativeProcess
                && !CodexBehaviorVerifier::rememberVerifiedEvidence(m_compatibility))
                m_compatibility.explanation += " Verification passed for this session, but protected storage was unavailable.";
        }
        else CodexBehaviorVerifier::forgetCachedEvidence(m_compatibility);
        emit compatibilityChanged(m_compatibility);
    });
    connect(&m_deadlineTimer, &QTimer::timeout, this, &CodexClient::deadlines);
    connect(&m_loginTimer, &QTimer::timeout, this, [this] {
        if (!m_login) return;
        const auto cancelled = cancelLogin();
        Q_UNUSED(cancelled);
    });
    connect(&transport, &ICodexTransport::started, this, [this](quint64 generation) {
        if (!m_active || generation != m_generation || m_initializeSent) return;
        m_initializeSent = true;
        const auto sent = request(Operation::Initialize, QJsonObject{{"clientInfo", QJsonObject{
            {"name", "devhub"}, {"title", "DevHub"}, {"version", DEVHUB_VERSION}}},
            {"capabilities", QJsonObject{{"experimentalApi", m_wire.profile().experimentalApi}}}});
        if (!sent) fatal(QStringLiteral("The initialize request could not be sent."), generation);
    });
    connect(&transport, &ICodexTransport::objectReceived, this, &CodexClient::receive);
    connect(&transport, &ICodexTransport::failed, this, [this](AppError, quint64 generation) {
        if (m_active && generation == m_generation) {
            disconnectClient(error(ErrorCode::CodexConnectionLost, QStringLiteral("The Codex connection was lost.")));
            if (m_transport && m_generation == generation && m_transportBusy) m_transport->stop();
        }
    });
    connect(&transport, &ICodexTransport::stopped, this, [this](int, bool, quint64 generation) {
        if (generation == m_generation) m_transportBusy = false;
        if (m_active && generation == m_generation) disconnectClient(error(ErrorCode::CodexConnectionLost, QStringLiteral("The Codex connection closed.")));
        emit backendStopped(generation);
    });
    connect(&transport, &QObject::destroyed, this, [this] {
        m_transport = nullptr;
        if (m_active) disconnectClient(error(ErrorCode::CodexConnectionLost, QStringLiteral("The Codex transport was destroyed.")));
    });
}
bool CodexClient::configureDynamicTools(QJsonArray specifications)
{
    if (m_active || m_transportBusy || specifications.size() > 32) return false;
    for (const auto& value : specifications) {
        const auto spec = value.toObject();
        const QString name = spec.value(QStringLiteral("name")).toString();
        if (!value.isObject() || !m_wire.acceptsDefinition(QStringLiteral("DynamicToolSpec"), value)
            || spec.value(QStringLiteral("type")) != QLatin1String("function")
            || !name.startsWith(QStringLiteral("devhub_"))) return false;
    }
    if (specifications.isEmpty()) {
        m_dynamicToolSpecifications = {};
        m_dynamicToolFingerprint.clear();
        return true;
    }
    // Every admitted version has an independently pinned tagged-tool contract.
    // Validate the inventory against all supported profiles before storing it.
    for (const auto& profile : CodexProtocolProfile::profiles()) {
        const CodexWireContract wire(profile.snapshotVersion);
        if (!wire.contains(QStringLiteral("ServerRequest:item/tool/call"))
            || !wire.contains(QStringLiteral("Response:DynamicToolCallResponse"))) return false;
        for (const auto& value : specifications)
            if (!wire.acceptsDefinition(QStringLiteral("DynamicToolSpec"), value)) return false;
    }
    m_dynamicToolSpecifications = std::move(specifications);
    // The selected native version belongs to the launch profile fingerprint;
    // inventory identity must not depend on the last version this client used.
    const auto identity = QJsonDocument(QJsonArray{QStringLiteral("devhub-dynamic-read-tools-v2"),
        m_dynamicToolSpecifications}).toJson(QJsonDocument::Compact);
    m_dynamicToolFingerprint = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    return true;
}
Result<void> CodexClient::launch(const CodexLaunchSpec& spec)
{
    const bool memory = m_transport && m_transport->kind() == CodexTransportKind::InMemoryTest;
    auto evidence = memory ? std::optional<CodexCompatibilityReport>{} : CodexSchemaInspector::evidence(spec.executable);
    if (!memory && !evidence && !m_active && !m_transportBusy) {
        m_compatibility = spec.executable.compatibility;
        m_compatibility.protocolCompatible = false; m_compatibility.runtimePolicyVerified = false;
        m_compatibility.state = CodexCompatibilityState::VerificationRequired;
        m_compatibility.reason = CodexCompatibilityReason::EvidenceMissing;
        m_compatibility.explanation = "Fresh executable-specific compatibility evidence is missing or stale. Recheck the executable.";
        emit compatibilityChanged(m_compatibility);
        return Result<void>::failure(error(ErrorCode::StartupFailed, m_compatibility.explanation));
    }
    const auto adapter = memory ? (spec.executable.compatibility.protocolCompatible
            ? spec.executable.compatibility.adapter : CodexProtocolProfile::fixture(spec.executable.version).id)
        : evidence ? evidence->adapter : QString{};
    if (!m_transport || m_active || m_transportBusy || spec.processGeneration <= m_generation
        || !CodexVersionSupport::isValidVersion(spec.executable.version) || !m_wire.setProfile(adapter, spec.executable.version)
        || !QFileInfo(spec.scratchDirectory).isAbsolute() || !QFileInfo(spec.scratchDirectory).isDir()
        || !CodexRuntimeProfile::acceptsEnvironment(spec.environment) || !CodexRuntimeProfile::acceptsLaunch(spec.arguments)) return Result<void>::failure(denied());
    m_compatibility = evidence.value_or(CodexCompatibilityReport{});
    if (memory) {
        m_compatibility.adapter = adapter; m_compatibility.reportedVersion = spec.executable.version;
        m_compatibility.protocolCompatible = true; m_compatibility.evidence = {"in-memory-fixture-only"};
    }
    m_runtimeInventoriesVerified = memory; m_inventoryReplies.clear(); m_inventoryFailure.clear(); m_mcpInventory.reset();
    m_generation = spec.processGeneration; m_active = true; m_initialized = false; m_profileVerified = false;
    const auto oldConfigurationWatches = m_configurationWatcher.files() + m_configurationWatcher.directories();
    if (!oldConfigurationWatches.isEmpty()) m_configurationWatcher.removePaths(oldConfigurationWatches);
    m_configurationPaths.clear(); m_configurationStamp.clear();
    if (!m_executableWatcher.files().isEmpty()) m_executableWatcher.removePaths(m_executableWatcher.files());
    if (!memory && !m_executableWatcher.addPath(m_compatibility.canonicalExecutable)) {
        m_active = false;
        m_compatibility.reason = CodexCompatibilityReason::RuntimeVerificationRequired;
        m_compatibility.explanation = "Changes to the selected executable could not be monitored. Reconnect to retry verification.";
        emit compatibilityChanged(m_compatibility);
        return Result<void>::failure(error(ErrorCode::StartupFailed, m_compatibility.explanation));
    }
    m_transportBusy = true; m_initializeSent = false;
    m_uncertain = false; m_interruptAccepted = false; m_scratch = QFileInfo(spec.scratchDirectory).canonicalFilePath();
    m_dynamicToolsEnabled = m_wire.profile().dynamicTools && !m_dynamicToolSpecifications.isEmpty()
        && !m_dynamicToolFingerprint.isEmpty();
    m_profileFingerprint = CodexRuntimeProfile::fingerprint(m_wire.profile().bindingSemantics, spec.arguments);
    QJsonObject environmentIdentity;
    for (const auto& key : spec.environment.keys()) environmentIdentity.insert(key.toCaseFolded(), spec.environment.value(key));
    m_environmentIdentity = CodexSchemaSurface::identity(environmentIdentity);
    m_launchArguments = spec.arguments;
    if (m_dynamicToolsEnabled) {
        const auto profileBytes = (m_profileFingerprint + QLatin1Char('|') + m_dynamicToolFingerprint).toUtf8();
        m_profileFingerprint = QString::fromLatin1(QCryptographicHash::hash(profileBytes, QCryptographicHash::Sha256).toHex());
    }
    m_profileFingerprint.prepend("semantic-v2:");
    m_legacyProfileFingerprints.clear();
    for (const auto& profile : CodexProtocolProfile::profiles())
        m_legacyProfileFingerprints << CodexRuntimeProfile::legacyFingerprint(profile.snapshotVersion, spec.arguments, m_dynamicToolFingerprint);
    m_expectedHome.clear();
    for (const auto& key : spec.environment.keys()) if (key.compare("CODEX_HOME", Qt::CaseInsensitive) == 0)
        m_expectedHome = QFileInfo(spec.environment.value(key)).canonicalFilePath();
    m_developmentLiveValidationAuthorized = CodexRuntimeProfile::developmentLiveInferenceAuthorized(
        spec.executable.path, spec.executable.version, spec.environment);
    m_initializeInfo = {}; m_account = {}; m_thread = {}; m_turnId.clear(); m_lastTurnId.clear();
    m_items.clear(); m_itemOrder.clear(); m_earlyTurns.clear(); m_earlyLogins.clear(); m_earlyBytes = 0; m_earlyLoginBytes = 0; m_serverRequests.clear(); m_resolvedServerRequests.clear();
    m_config.reset(); m_requirements.reset(); ++m_accountEpoch;
    m_accountDeferred.clear(); m_accountDeferredBytes = 0; m_accountRefreshPending = false;
    emit compatibilityChanged(m_compatibility);
    m_deadlineTimer.start(); m_transport->launch(spec);
    return Result<void>::success();
}
void CodexClient::stop()
{
    m_behaviorVerifier.cancel();
    const auto generation = m_generation;
    if (m_active) disconnectClient(error(ErrorCode::OperationCancelled, QStringLiteral("The Codex connection is stopping.")));
    if (m_transport && m_transportBusy && m_generation == generation) m_transport->stop();
}
bool CodexClient::verifyCompatibility()
{
    if (!m_active || !m_initialized || !m_profileVerified || m_behaviorVerifier.active() || !m_transport
        || m_transport->kind() != CodexTransportKind::NativeProcess || !m_turnId.isEmpty()
        || !m_pending.isEmpty() || m_login || m_accountRefreshPending) return false;
    m_sessionBehaviorEvidence.remove(behaviorEvidenceKey(m_compatibility));
    CodexBehaviorVerifier::forgetCachedEvidence(m_compatibility);
    m_compatibility.runtimePolicyVerified = false;
    m_behaviorVerificationGeneration = m_generation;
    m_compatibility.state = CodexCompatibilityState::VerificationRequired;
    m_compatibility.reason = CodexCompatibilityReason::RuntimeVerificationRequired;
    m_compatibility.verificationInProgress = true;
    m_compatibility.explanation = "Verifying restrictions with isolated synthetic local checks…";
    emit compatibilityChanged(m_compatibility);
    const bool started = m_behaviorVerifier.start(m_compatibility, m_dynamicToolSpecifications);
    if (!started) {
        m_compatibility.verificationInProgress = false;
        m_compatibility.explanation = "Offline verification requires native Python 3 on PATH and an unchanged inspected executable.";
        emit compatibilityChanged(m_compatibility);
    }
    return started;
}
bool CodexClient::hasPending(Operation operation) const
{
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) if (it->operation == operation) return true;
    return false;
}
Result<qint64> CodexClient::request(Operation operation, QJsonValue params, QString expectedThread, QString argument)
{
    if (!m_active || !m_transport || (operation != Operation::Initialize && !m_initialized)
        || hasPending(operation) || m_pending.size() >= m_limits.pendingRequests || m_nextId > 9007199254740991LL)
        return Result<qint64>::failure(denied());
    const bool scopedOperation = operation == Operation::ThreadStart || operation == Operation::ThreadResume
        || operation == Operation::ThreadRead || operation == Operation::TurnStart;
    if (scopedOperation && m_transport->kind() != CodexTransportKind::InMemoryTest && !liveInferenceGateOpen(true))
        return Result<qint64>::failure(denied());
    const qint64 id = m_nextId++;
    const auto name = method(operation);
    const QJsonObject object{{"id", id}, {"method", name}, {"params", params}};
    if (!m_wire.accepts("ClientRequest:" + name, object)) return Result<qint64>::failure(invalidResponse());
    const int timeout = operation == Operation::LoginStart ? m_limits.loginAcknowledgementMilliseconds : m_limits.acknowledgementMilliseconds;
    const Pending pending{operation, m_clock.elapsed() + timeout, m_generation, m_accountEpoch, std::move(expectedThread), std::move(argument)};
    // Register before sending: a deterministic fake may respond synchronously.
    m_pending.insert(id, pending);
    const auto sent = m_transport->sendObject(object);
    if (!sent) {
        if (m_pending.remove(id)) failPending(id, pending, error(ErrorCode::IoFailure, QStringLiteral("The Codex request could not be queued.")), false);
        return Result<qint64>::failure(error(ErrorCode::IoFailure, QStringLiteral("The Codex request could not be queued.")));
    }
    return Result<qint64>::success(id);
}
Result<qint64> CodexClient::readAccount(bool refreshToken)
{
    return request(Operation::Account, QJsonObject{{"refreshToken", refreshToken}});
}
Result<qint64> CodexClient::startLogin(CodexLoginMode mode)
{
    if (m_behaviorVerifier.active() || m_login || hasPending(Operation::LoginStart) || hasPending(Operation::LoginCancel) || !m_turnId.isEmpty()
        || hasPending(Operation::TurnStart) || hasPending(Operation::ThreadStart) || hasPending(Operation::ThreadResume)) return Result<qint64>::failure(denied());
    m_earlyLogins.clear(); m_earlyLoginBytes = 0;
    const QString type = mode == CodexLoginMode::Browser ? QStringLiteral("chatgpt") : QStringLiteral("chatgptDeviceCode");
    return request(Operation::LoginStart, QJsonObject{{"type", type}}, {}, type);
}
Result<qint64> CodexClient::cancelLogin()
{
    if (!m_login) return Result<qint64>::failure(denied());
    const auto id = m_login->loginId;
    clearLogin(); // Revoke the UI challenge before any late completion.
    emit loginFinished(id, false);
    return request(Operation::LoginCancel, QJsonObject{{"loginId", id}}, {}, id);
}
Result<qint64> CodexClient::listModels(const QString& cursor)
{
    if (cursor.size() > 512) return Result<qint64>::failure(denied());
    QJsonObject params{{"includeHidden", false}, {"limit", 100}};
    if (!cursor.isEmpty()) params.insert("cursor", cursor);
    return request(Operation::Models, params);
}
Result<void> CodexClient::verifyRuntime()
{
    if (!m_initialized || m_behaviorVerifier.active() || hasPending(Operation::Config) || hasPending(Operation::Requirements) || !m_turnId.isEmpty()
        || hasPending(Operation::TurnStart) || hasPending(Operation::ThreadStart) || hasPending(Operation::ThreadResume)) return Result<void>::failure(denied());
    m_profileVerified = false; m_config.reset(); m_requirements.reset(); m_mcpInventory.reset();
    m_compatibility.runtimePolicyVerified = false;
    m_compatibility.state = CodexCompatibilityState::VerificationRequired;
    if (m_transport->kind() != CodexTransportKind::InMemoryTest) {
        m_runtimeInventoriesVerified = false; m_inventoryReplies.clear(); m_inventoryFailure.clear();
    }
    const auto config = request(Operation::Config, QJsonObject{{"includeLayers", true}, {"cwd", m_scratch}});
    if (!config) return Result<void>::failure(config.error());
    const auto requirements = request(Operation::Requirements, QJsonValue(QJsonValue::Null));
    if (!requirements) return Result<void>::failure(requirements.error());
    if (m_transport->kind() != CodexTransportKind::InMemoryTest) {
        const auto features = request(Operation::Features, QJsonObject{{"limit", 200}});
        const auto hooks = request(Operation::Hooks, QJsonObject{{"cwds", QJsonArray{m_scratch}}});
        const auto mcp = request(Operation::Mcp, QJsonObject{{"limit", 100}});
        const auto plugins = request(Operation::Plugins, QJsonObject{{"cwds", QJsonArray{m_scratch}}});
        if (!features || !hooks || !mcp || !plugins) return Result<void>::failure(denied());
    }
    return Result<void>::success();
}
bool CodexClient::bindingMatches(const CodexThreadBinding& binding) const
{
    const QString homeKey = QString::fromLatin1(QCryptographicHash::hash(m_initializeInfo.codexHome.toUtf8(), QCryptographicHash::Sha256).toHex());
    return m_initialized && m_profileVerified && !m_accountRefreshPending && stringId(binding.threadId) && m_account.mode == CodexAccountMode::ChatGpt
        && m_account.requiresOpenaiAuth && !m_account.identityFingerprint.isEmpty()
        && binding.accountFingerprint == m_account.identityFingerprint
        && (binding.codexHome == m_initializeInfo.codexHome || (!binding.codexHomeKey.isEmpty() && binding.codexHomeKey == homeKey))
        && binding.scratchDirectory == m_scratch
        && (binding.profileFingerprint == m_profileFingerprint || m_legacyProfileFingerprints.contains(binding.profileFingerprint));
}
Result<qint64> CodexClient::startThread(const QString& model)
{
    if (!canStartConversation() || !m_initialized || !m_profileVerified || m_account.mode != CodexAccountMode::ChatGpt || !m_account.requiresOpenaiAuth
        || m_account.identityFingerprint.isEmpty() || hasPending(Operation::Account) || !stringId(model) || !m_turnId.isEmpty() || m_uncertain
        || hasPending(Operation::TurnStart) || hasPending(Operation::ThreadResume) || hasPending(Operation::ThreadRead)
        || m_login || hasPending(Operation::LoginStart)) return Result<qint64>::failure(denied());
    return request(Operation::ThreadStart, QJsonObject{{"cwd", m_scratch}, {"model", model}, {"sandbox", "read-only"},
        {"approvalPolicy", "on-request"}, {"historyMode", "legacy"}, {"environments", QJsonArray{}},
        {"runtimeWorkspaceRoots", QJsonArray{}}, {"selectedCapabilityRoots", QJsonArray{}},
        {"dynamicTools", m_dynamicToolsEnabled ? m_dynamicToolSpecifications : QJsonArray{}},
        {"allowProviderModelFallback", false}});
}
Result<qint64> CodexClient::resumeThread(const CodexThreadBinding& binding)
{
    if (!bindingMatches(binding) || (m_transport->kind() != CodexTransportKind::InMemoryTest && !liveInferenceGateOpen())
        || m_uncertain || !m_turnId.isEmpty() || hasPending(Operation::TurnStart)
        || hasPending(Operation::ThreadStart) || hasPending(Operation::ThreadRead) || hasPending(Operation::Account)
        || m_login || hasPending(Operation::LoginStart)) return Result<qint64>::failure(denied());
    // Resume has no environments/dynamicTools parameters. The alpha runtime
    // defaults to paginated history; request full items from the oldest turn
    // through one beyond our cap so truncated history cannot appear complete.
    QJsonObject params{{"threadId", binding.threadId}, {"cwd", m_scratch},
        {"sandbox", "read-only"}, {"approvalPolicy", "on-request"}, {"runtimeWorkspaceRoots", QJsonArray{}}};
    if (m_wire.profile().initialTurnsPage)
        params.insert("initialTurnsPage", QJsonObject{{"itemsView", "full"}, {"limit", m_limits.historyTurns + 1}, {"sortDirection", "asc"}});
    return request(Operation::ThreadResume, params, binding.threadId);
}
Result<qint64> CodexClient::readThread(const CodexThreadBinding& binding)
{
    if (!bindingMatches(binding) || !m_turnId.isEmpty() || hasPending(Operation::TurnStart)
        || hasPending(Operation::ThreadStart) || hasPending(Operation::ThreadResume)) return Result<qint64>::failure(denied());
    return request(Operation::ThreadRead, QJsonObject{{"threadId", binding.threadId}, {"includeTurns", true}}, binding.threadId);
}
bool CodexClient::canSend() const
{
    return m_active && !m_accountRefreshPending && bindingMatches(m_thread) && !m_uncertain && m_turnId.isEmpty() && !hasPending(Operation::TurnStart)
        && !hasPending(Operation::ThreadStart) && !hasPending(Operation::ThreadResume) && !hasPending(Operation::ThreadRead) && !m_login
        && !hasPending(Operation::LoginStart) && !hasPending(Operation::Account) && m_transport
        && (m_transport->kind() == CodexTransportKind::InMemoryTest || liveInferenceGateOpen());
}
bool CodexClient::canStartConversation() const
{
    return m_active && m_initialized && m_profileVerified && !m_accountRefreshPending && !m_uncertain && m_turnId.isEmpty()
        && m_account.mode == CodexAccountMode::ChatGpt && m_account.requiresOpenaiAuth
        && !m_account.identityFingerprint.isEmpty() && !hasPending(Operation::Account)
        && !hasPending(Operation::ThreadStart) && !hasPending(Operation::ThreadResume)
        && !hasPending(Operation::ThreadRead) && !hasPending(Operation::TurnStart)
        && !m_login && !hasPending(Operation::LoginStart) && m_transport
        && (m_transport->kind() == CodexTransportKind::InMemoryTest || liveInferenceGateOpen());
}
bool CodexClient::liveInferenceGateOpen(bool revalidateFiles) const
{
    return (CodexRuntimeProfile::productionChatEnabled(m_compatibility) || m_developmentLiveValidationAuthorized)
        && !m_compatibility.binaryIdentity.isEmpty()
        // View refreshes use verified state and watcher-driven revocation.
        // Every native chat/history request and tool admission rechecks metadata
        // before acting, including changes whose watcher event is still queued.
        && (!revalidateFiles || (configurationStamp() == m_configurationStamp
            && CodexSchemaInspector::matchesBinaryIdentity(m_compatibility.canonicalExecutable, m_compatibility.binaryIdentity)));
}
Result<qint64> CodexClient::startTurn(const QString& text)
{
    if (!canSend() || text.trimmed().isEmpty() || text.toUtf8().size() > m_limits.itemTextBytes) return Result<qint64>::failure(denied());
    m_items.clear(); m_itemOrder.clear(); m_earlyTurns.clear(); m_earlyBytes = 0; m_interruptAccepted = false;
    return request(Operation::TurnStart, QJsonObject{{"threadId", m_thread.threadId},
        {"input", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}, {"text_elements", QJsonArray{}}}}},
        {"environments", QJsonArray{}}, {"runtimeWorkspaceRoots", QJsonArray{}}, {"approvalPolicy", "on-request"},
        {"sandboxPolicy", QJsonObject{{"type", "readOnly"}, {"networkAccess", false}}}}, m_thread.threadId);
}
Result<qint64> CodexClient::interruptTurn()
{
    if (!m_initialized || m_turnId.isEmpty() || m_interruptAccepted || hasPending(Operation::TurnInterrupt)) return Result<qint64>::failure(denied());
    return request(Operation::TurnInterrupt, QJsonObject{{"threadId", m_thread.threadId}, {"turnId", m_turnId}}, m_thread.threadId, m_turnId);
}
void CodexClient::receive(const QJsonObject& object, quint64 generation)
{
    if (!m_active || generation != m_generation) { ++m_ignored; return; }
    if (m_accountRefreshPending && scopeMessage(object)) {
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact).size();
        if (m_accountDeferred.size() >= m_limits.earlyEvents || bytes > m_limits.earlyEventBytes - m_accountDeferredBytes) {
            fatal(QStringLiteral("The account verification event limit was exceeded.")); return;
        }
        m_accountDeferred.append({false, object}); m_accountDeferredBytes += bytes; return;
    }
    if (object.contains("method")) {
        if (object.contains("id")) serverRequest(object);
        else notification(object);
    } else response(object);
}
bool CodexClient::scopeMessage(const QJsonObject& object) const
{
    if (object.contains("method")) {
        const auto name = object.value("method").toString();
        return name.startsWith("turn/") || name.startsWith("item/") || name.startsWith("thread/");
    }
    const auto pending = m_pending.constFind(object.value("id").toInteger());
    return pending != m_pending.cend() && (pending->operation == Operation::ThreadStart
        || pending->operation == Operation::ThreadResume || pending->operation == Operation::ThreadRead
        || pending->operation == Operation::TurnStart);
}
void CodexClient::response(const QJsonObject& object)
{
    const auto value = object.value("id");
    if (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble() || value.toDouble() < 1
        || value.toDouble() > 9007199254740991.0) { ++m_ignored; return; }
    const auto id = value.toInteger();
    const auto found = m_pending.find(id);
    if (found == m_pending.end()) { ++m_ignored; return; }
    const auto pending = found.value(); m_pending.erase(found);
    if (pending.generation != m_generation || (pending.accountEpoch != m_accountEpoch && pending.operation == Operation::Account)) { ++m_ignored; return; }
    if (pending.deadline <= m_clock.elapsed()) {
        failPending(id, pending, error(ErrorCode::CodexRequestTimeout, QStringLiteral("The Codex acknowledgement timed out.")), true);
        if (pending.operation == Operation::Initialize || pending.operation == Operation::LoginStart)
            fatal(QStringLiteral("Codex startup or login became uncertain."), pending.generation);
        return;
    }
    if (object.contains("error")) {
        const auto remoteError = object.value("error").toObject();
        if (!m_wire.accepts("Response:JSONRPCError", object)) { failPending(id, pending, invalidResponse(), true); fatal(QStringLiteral("An invalid error response was received."), pending.generation); return; }
        const auto code = remoteError.value("code").toInteger();
        auto safeError = error(ErrorCode::IoFailure, QStringLiteral("Codex rejected the operation."));
        safeError.nativeCode = code; // Never forward raw remote messages/data.
        if (code == 401) { expireAccount(); safeError.code = ErrorCode::AuthenticationFailed; }
        failPending(id, pending, safeError, false);
        if (pending.operation == Operation::Initialize)
            fatal(QStringLiteral("Codex initialization was rejected. Reconnect to try again."), pending.generation);
        return;
    }
    const auto result = object.value("result");
    if (!m_wire.accepts(responseContract(pending.operation), result) || !result.isObject()) {
        failPending(id, pending, invalidResponse(), true); fatal(QStringLiteral("A required response shape was invalid."), pending.generation); return;
    }
    if (!decodeResult(id, pending, result.toObject())) {
        failPending(id, pending, invalidResponse(), true);
        fatal(QStringLiteral("The %1 response did not match the active Codex scope.").arg(method(pending.operation)), pending.generation); return;
    }
    emit requestSucceeded(id, method(pending.operation));
}
bool CodexClient::decodeResult(qint64, const Pending& pending, const QJsonObject& result)
{
    switch (pending.operation) {
    case Operation::Initialize: {
        const auto home = result.value("codexHome").toString();
        const QFileInfo homeInfo(home);
        if (!homeInfo.isAbsolute() || !homeInfo.isDir() || (!m_expectedHome.isEmpty() && homeInfo.canonicalFilePath() != m_expectedHome)) return false;
        m_initializeInfo = {homeInfo.canonicalFilePath(), result.value("userAgent").toString(),
            result.value("platformFamily").toString(), result.value("platformOs").toString()};
        const auto sent = m_transport->sendObject(QJsonObject{{"method", "initialized"}});
        if (!sent) return false;
        m_initialized = true;
        const auto accountRequest = readAccount();
        const auto profileRequest = verifyRuntime();
        if (!accountRequest || !profileRequest) return false;
        emit initialized(m_initializeInfo); return true;
    }
    case Operation::Account: {
        CodexAccount account;
        account.requiresOpenaiAuth = result.value("requiresOpenaiAuth").toBool();
        const auto wireAccount = result.value("account");
        if (wireAccount.isUndefined() || wireAccount.isNull()) account.mode = CodexAccountMode::SignedOut;
        else {
            const auto fields = wireAccount.toObject(); const auto type = fields.value("type").toString();
            account.mode = type == "chatgpt" ? CodexAccountMode::ChatGpt : type == "apiKey" ? CodexAccountMode::ApiKey : CodexAccountMode::Other;
            account.planType = fields.value("planType").toString();
            const auto routing = result.value("workspaceRouting").toObject();
            const auto origin = routing.value("backendOrigin").toString();
            const auto accountId = routing.value("chatgptAccountId").toString();
            if (account.mode == CodexAccountMode::ChatGpt && !origin.isEmpty() && !accountId.isEmpty())
                account.identityFingerprint = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonArray{
                    "devhub-codex-account-v1", "chatgpt", origin, accountId}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        }
        if (m_account.mode != account.mode || m_account.identityFingerprint != account.identityFingerprint
            || m_account.requiresOpenaiAuth != account.requiresOpenaiAuth) {
            ++m_accountEpoch;
            if (!m_turnId.isEmpty() || hasPending(Operation::TurnStart)) {
                fatal(QStringLiteral("The account scope changed during an active turn."), pending.generation);
                return false;
            }
        }
        m_accountRefreshPending = false;
        const auto deferred = std::exchange(m_accountDeferred, {}); m_accountDeferredBytes = 0;
        m_account = account; emit accountChanged(m_account);
        for (const auto& envelope : deferred) {
            if (!m_active || m_generation != pending.generation) break;
            if (envelope.first) (void)resolveDynamicToolCall(envelope.second.value("id"), envelope.second.value("result").toObject());
            else receive(envelope.second, pending.generation);
        }
        return true;
    }
    case Operation::LoginStart: {
        if (result.value("type").toString() != pending.argument || !stringId(result.value("loginId").toString())) return false;
        const bool device = pending.argument == "chatgptDeviceCode";
        CodexLoginChallenge challenge{device ? CodexLoginMode::DeviceCode : CodexLoginMode::Browser,
            result.value("loginId").toString(), QUrl(result.value(device ? "verificationUrl" : "authUrl").toString()), result.value("userCode").toString()};
        if (!CodexRuntimeProfile::acceptsLoginUrl(challenge.url, device) || (device && (challenge.userCode.isEmpty() || challenge.userCode.size() > 64))) return false;
        m_login = challenge;
        const auto early = std::exchange(m_earlyLogins, {});
        for (const auto& event : early) loginCompleted(event);
        if (m_login) { m_loginTimer.start(m_limits.loginLifetimeMilliseconds); emit loginChallenge(*m_login); }
        return true;
    }
    case Operation::LoginCancel: return true;
    case Operation::Models: {
        CodexModelPage page; page.nextCursor = result.value("nextCursor").toString();
        const auto data = result.value("data").toArray();
        if (data.size() > 100 || page.nextCursor.size() > 512) return false;
        for (const auto& record : data) {
            const auto model = record.toObject();
            if (!stringId(model.value("id").toString()) || !stringId(model.value("model").toString())) return false;
            CodexModel value{model.value("id").toString(), model.value("model").toString(), model.value("displayName").toString(),
                model.value("hidden").toBool(), model.value("isDefault").toBool(), true,
                model.value("availabilityNux").toObject().value("message").toString(), false};
            if (model.contains("inputModalities")) value.supportsText = model.value("inputModalities").toArray().contains("text");
            page.models.append(value);
        }
        emit modelsReceived(page); return true;
    }
    case Operation::Config: m_config = result; finishProfileCheck(); return true;
    case Operation::Requirements: m_requirements = result; finishProfileCheck(); return true;
    case Operation::Features:
    case Operation::Hooks:
    case Operation::Mcp:
    case Operation::Plugins: {
        // MCP entries need the effective config to distinguish disabled entries
        // from active servers. Replies can arrive in either order.
        if (pending.operation == Operation::Mcp) m_mcpInventory = result;
        const auto reasons = pending.operation == Operation::Mcp ? QStringList{}
            : CodexRuntimeProfile::inventoryRejectionReasons(method(pending.operation), result);
        if (!reasons.isEmpty()) {
            auto failures = m_inventoryFailure.split(';', Qt::SkipEmptyParts);
            failures.append(reasons); failures.removeDuplicates();
            m_inventoryFailure = failures.join(';');
        }
        m_inventoryReplies.insert(method(pending.operation));
        m_runtimeInventoriesVerified = m_inventoryReplies.size() == 4 && m_inventoryFailure.isEmpty();
        finishProfileCheck(); return true;
    }
    case Operation::ThreadStart:
    case Operation::ThreadResume:
    case Operation::ThreadRead: {
        if (pending.accountEpoch != m_accountEpoch) return false;
        CodexThread thread;
        auto threadValue = result.value("thread").toObject();
        const bool paginatedResume = pending.operation == Operation::ThreadResume
            && m_wire.profile().initialTurnsPage
            && (threadValue.value("historyMode") == "paginated" || result.value("initialTurnsPage").isObject());
        if (paginatedResume) {
            if (threadValue.value("historyMode") != "legacy" && threadValue.value("historyMode") != "paginated") return false;
            const auto page = result.value("initialTurnsPage");
            if (!page.isObject() || !page.toObject().value("nextCursor").toString().isEmpty()
                || QJsonDocument(result).toJson(QJsonDocument::Compact).size() > m_limits.historyBytes) return false;
            // Only the explicitly requested complete page supplies transcript
            // history. Backwards cursors mark boundaries, not missing pages.
            threadValue.insert("turns", page.toObject().value("data"));
            threadValue.insert("historyMode", "legacy");
        }
        if (!decodeThread(threadValue, thread, pending.operation != Operation::ThreadStart,
                paginatedResume && m_wire.profile().scratchEnvironmentOnResume)) return false;
        if (!pending.expectedThread.isEmpty() && thread.binding.threadId != pending.expectedThread) return false;
        if (pending.operation != Operation::ThreadRead) {
            if (!matchesScratchDirectory(result.value("cwd").toString()) || result.value("modelProvider") != "openai"
                || result.value("approvalPolicy") != "on-request" || result.value("sandbox").toObject().value("type") != "readOnly"
                || result.value("sandbox").toObject().value("networkAccess").toBool()
                || !result.value("runtimeWorkspaceRoots").toArray().isEmpty()) return false;
            if (!paginatedResume) {
                if (pending.operation == Operation::ThreadResume && (!result.value("initialTurnsPage").isNull() && !result.value("initialTurnsPage").isUndefined())) return false;
                if (!result.value("turnsBackwardsCursor").toString().isEmpty() || !result.value("itemsBackwardsCursor").toString().isEmpty()) return false;
            }
            m_thread = thread.binding; m_lastTurnId.clear();
        }
        emit threadReceived(thread); return true;
    }
    case Operation::TurnStart: {
        if (pending.accountEpoch != m_accountEpoch || pending.expectedThread != m_thread.threadId) return false;
        CodexTurn turn;
        if (!decodeTurn(result.value("turn").toObject(), pending.expectedThread, turn, true) || turn.id == m_lastTurnId) return false;
        m_turnId = turn.id; emit turnAccepted(turn.threadId, turn.id);
        const auto early = std::exchange(m_earlyTurns, {}); m_earlyBytes = 0;
        for (const auto& event : early) {
            turnEvent(event);
            if (!m_active || m_generation != pending.generation) return false;
        }
        if (m_turnId == turn.id) {
            for (const auto& item : turn.items) if (!mergeItem(item)) return false;
            if (terminal(turn.status)) finishTurn(turn);
        }
        return true;
    }
    case Operation::TurnInterrupt:
        if (!m_turnId.isEmpty() && pending.argument == m_turnId) m_interruptAccepted = true;
        return true;
    }
    return false;
}
void CodexClient::finishProfileCheck()
{
    if (!m_config || !m_requirements) return;
    auto reasons = CodexRuntimeProfile::rejectionReasons(*m_config, *m_requirements);
    // Config conflicts (including the bounded inherited-MCP relaunch) are
    // actionable immediately. Success waits for all inventories as well.
    if (reasons.isEmpty() && !m_runtimeInventoriesVerified && m_inventoryReplies.size() < 4) return;
    if (!m_inventoryFailure.isEmpty()) reasons << m_inventoryFailure.split(';', Qt::SkipEmptyParts);
    if (m_mcpInventory) reasons << CodexRuntimeProfile::inventoryRejectionReasons("mcpServerStatus/list", *m_mcpInventory, *m_config);
    QStringList restrictedArguments;
    // Discover names through the supported RPC, never by reading config/auth
    // files. Do not create a thread until the relaunched effective config passes.
    const bool onlyInheritedMcp = reasons.contains("inherited-mcp-enabled")
        && std::all_of(reasons.cbegin(), reasons.cend(), [](const QString& reason) {
            return reason == "inherited-mcp-enabled" || reason == "inherited-mcp-active";
        });
    if (onlyInheritedMcp) {
        const auto servers = m_config->value("config").toObject().value("mcp_servers").toObject();
        const auto arguments = CodexRuntimeProfile::launchArguments(servers.keys());
        if (arguments && arguments.value() != m_launchArguments) restrictedArguments = arguments.value();
    }
    const auto configurationIdentity = CodexSchemaSurface::identity(QJsonObject{{"config", *m_config}, {"requirements", *m_requirements},
        {"processEnvironment", m_environmentIdentity}});
    m_compatibility.configurationIdentity = configurationIdentity;
    m_compatibility.policyIdentity = m_profileFingerprint;
    if (reasons.isEmpty() && m_transport->kind() != CodexTransportKind::InMemoryTest && !watchConfiguration(*m_config))
        reasons << "configuration-watch-unavailable";
    m_config.reset(); m_requirements.reset(); m_profileVerified = reasons.isEmpty() && m_runtimeInventoriesVerified;
    if (!m_profileVerified) {
        m_sessionBehaviorEvidence.clear();
        CodexBehaviorVerifier::forgetCachedEvidence(m_compatibility);
    }
    // Historical fixtures did not bind their verdicts to a binary digest. They
    // define our verification cases but cannot authorize an installed binary.
    const bool restored = m_profileVerified && m_transport->kind() == CodexTransportKind::NativeProcess
        && CodexBehaviorVerifier::cachedEvidenceMatches(m_compatibility);
    const bool behavioral = restored || m_sessionBehaviorEvidence.contains(behaviorEvidenceKey(m_compatibility));
    m_compatibility.runtimePolicyVerified = m_profileVerified && behavioral;
    m_compatibility.state = !reasons.isEmpty() ? CodexCompatibilityState::Incompatible
        : behavioral ? CodexCompatibilityState::Compatible : CodexCompatibilityState::VerificationRequired;
    m_compatibility.reason = !reasons.isEmpty() ? CodexCompatibilityReason::RuntimeRestrictionFailed
        : behavioral ? CodexCompatibilityReason::None : CodexCompatibilityReason::RuntimeVerificationRequired;
    m_compatibility.explanation = !reasons.isEmpty() ? QStringLiteral("Runtime restrictions failed: %1.").arg(reasons.join("; "))
        : behavioral ? QStringLiteral("Protocol and effective runtime restrictions are verified for this profile.")
        : QStringLiteral("Protocol compatible; this release needs behavioral verification of disabled native tools before sending.");
    if (m_compatibility.runtimePolicyVerified) m_compatibility.evidence << "session-behavior-evidence-match";
    if (restored) m_compatibility.evidence << "protected-behavior-cache-match-v1";
    emit compatibilityChanged(m_compatibility);
    emit profileChecked(m_profileVerified, reasons);
    if (!restrictedArguments.isEmpty()) emit restrictedLaunchRequired(restrictedArguments, m_generation);
}
QString CodexClient::configurationStamp() const
{
    QJsonObject values;
    // Observe metadata only. Authentication/configuration contents remain owned
    // by Codex and are obtained only through the validated config/read RPC.
    for (const auto& path : m_configurationPaths) {
        const QFileInfo info(path);
        values.insert(path, QJsonArray{info.exists(), info.size(), info.lastModified().toMSecsSinceEpoch(), info.canonicalFilePath()});
    }
    return CodexSchemaSurface::identity(values);
}
bool CodexClient::watchConfiguration(const QJsonObject& configResult)
{
    m_configurationPaths = {QDir(m_initializeInfo.codexHome).filePath("config.toml"),
        QDir(m_initializeInfo.codexHome).filePath("requirements.toml"), QDir(m_scratch).filePath(".codex/config.toml")};
    for (const auto& value : configResult.value("layers").toArray()) {
        const auto source = value.toObject().value("name").toObject();
        auto path = source.value("file").toString();
        if (source.value("type") == "project") path = QDir(source.value("dotCodexFolder").toString()).filePath("config.toml");
        if (QFileInfo(path).isAbsolute() && path.size() <= 4096) m_configurationPaths.insert(QDir::cleanPath(path));
    }
    QSet<QString> watches;
    for (const auto& path : m_configurationPaths) {
        if (QFileInfo::exists(path)) watches.insert(path);
        auto parent = QFileInfo(path).absoluteDir();
        while (!parent.exists() && parent.cdUp()) {}
        if (parent.exists()) watches.insert(parent.absolutePath());
    }
    const auto oldWatches = m_configurationWatcher.files() + m_configurationWatcher.directories();
    if (!oldWatches.isEmpty()) m_configurationWatcher.removePaths(oldWatches);
    m_configurationStamp = configurationStamp();
    return m_configurationWatcher.addPaths(watches.values()).isEmpty();
}
void CodexClient::invalidateCompatibility(CodexCompatibilityReason reason, const QString& explanation)
{
    CodexBehaviorVerifier::forgetCachedEvidence(m_compatibility);
    auto report = m_compatibility; m_sessionBehaviorEvidence.clear(); stop();
    report.runtimePolicyVerified = false; report.verificationInProgress = false;
    report.state = reason == CodexCompatibilityReason::ExecutableChanged
        ? CodexCompatibilityState::Incompatible : CodexCompatibilityState::VerificationRequired;
    report.reason = reason; report.explanation = explanation;
    m_compatibility = report; emit compatibilityChanged(m_compatibility);
}
bool CodexClient::decodeItem(const QJsonObject& value, const QString& threadId, const QString& turnId, CodexItem& item) const
{
    item = {threadId, turnId, value.value("id").toString(), value.value("type").toString(), {}, true, false};
    if (!stringId(item.id)) return false;
    if (item.type == "dynamicToolCall") {
        // Retain identity only; arguments and contentItems never enter text state.
        if (!m_dynamicToolsEnabled || !value.value("namespace").toString().isEmpty()) return false;
        const QString name = value.value("tool").toString();
        for (const auto& specification : m_dynamicToolSpecifications)
            if (specification.toObject().value("name").toString() == name) return true;
        return false;
    }
    if (!safeTextItem(item.type)) return false;
    if (item.type == "agentMessage" || item.type == "plan") item.text = value.value("text").toString();
    if (item.type == "userMessage") {
        for (const auto& input : value.value("content").toArray()) {
            const auto object = input.toObject();
            if (object.value("type") != "text") return false;
            item.text += object.value("text").toString();
        }
    }
    return item.text.toUtf8().size() <= m_limits.itemTextBytes;
}
bool CodexClient::decodeTurn(const QJsonObject& value, const QString& threadId, CodexTurn& turn, bool live) const
{
    turn.threadId = threadId; turn.id = value.value("id").toString();
    const auto view = value.value("itemsView").toString("full");
    // Alpha starts omit items; successful completion includes only the final
    // agent message. Merge that summary into the retained item stream. Stored
    // history still requires full items.
    const bool unloadedLive = live && m_wire.profile().summarizedLiveItems
        && view == "notLoaded" && value.value("items").isArray() && value.value("items").toArray().isEmpty();
    const auto items = value.value("items").toArray();
    const bool summaryLive = live && m_wire.profile().summarizedLiveItems
        && view == "summary" && value.value("status") == "completed"
        && items.size() == 1 && items.first().toObject().value("type") == "agentMessage";
    if (!stringId(turn.id) || (view != "full" && !unloadedLive && !summaryLive)) return false;
    const auto status = value.value("status").toString();
    if (status == "completed") turn.status = CodexTurnStatus::Completed;
    else if (status == "failed") turn.status = CodexTurnStatus::Failed;
    else if (status == "interrupted") turn.status = CodexTurnStatus::Interrupted;
    else if (status == "inProgress") turn.status = CodexTurnStatus::InProgress;
    else return false;
    const auto failure = value.value("error");
    if (failure.isObject()) {
        turn.hasError = true;
        const auto info = failure.toObject().value("codexErrorInfo");
        turn.error = error(info == "unauthorized" ? ErrorCode::AuthenticationFailed : ErrorCode::IoFailure,
            info == "unauthorized" ? QStringLiteral("Codex authentication needs to be refreshed.") : QStringLiteral("The Codex turn failed."));
    }
    if (turn.status == CodexTurnStatus::Completed && turn.hasError) return false;
    if (items.size() > m_limits.itemsPerTurn) return false;
    QSet<QString> ids;
    for (const auto& record : items) {
        CodexItem item;
        if (!decodeItem(record.toObject(), threadId, turn.id, item) || ids.contains(item.id)) return false;
        ids.insert(item.id); item.completed = terminal(turn.status); turn.items.append(item);
    }
    return true;
}
bool CodexClient::matchesScratchDirectory(const QString& path) const
{
    const QFileInfo directory(path);
    if (!directory.isAbsolute() || !directory.isDir()) return false;
    const auto canonical = directory.canonicalFilePath();
    if (canonical.isEmpty()) return false;
#ifdef Q_OS_WIN
    return canonical.compare(m_scratch, Qt::CaseInsensitive) == 0;
#else
    return canonical == m_scratch;
#endif
}
bool CodexClient::decodeThread(const QJsonObject& value, CodexThread& thread, bool allowUnassignedEnvironments,
    bool allowScratchEnvironment) const
{
    const auto id = value.value("id").toString();
    const auto environments = value.value("environments");
    // Persisted legacy history omits environment assignments or returns null.
    // New threads must acknowledge our explicit [] and every turn sends [] again.
    const bool unassigned = allowUnassignedEnvironments && (environments.isNull() || environments.isUndefined());
    // Alpha resume reconstructs one local environment even when start requested
    // []. It grants no additional root: verify its cwd and require empty roots.
    const auto assignments = environments.toArray();
    const auto local = assignments.size() == 1 ? assignments.first().toObject() : QJsonObject{};
    const bool scratchOnly = allowScratchEnvironment && assignments.size() == 1
        && stringId(local.value("environmentId").toString()) && matchesScratchDirectory(local.value("cwd").toString())
        && local.value("runtimeWorkspaceRoots").isArray() && local.value("runtimeWorkspaceRoots").toArray().isEmpty();
    if (!stringId(id) || !matchesScratchDirectory(value.value("cwd").toString())
        || value.value("modelProvider") != "openai" || value.value("historyMode").toString("legacy") != "legacy"
        || (!unassigned && !scratchOnly && (!environments.isArray() || !assignments.isEmpty()))
        || QJsonDocument(value).toJson(QJsonDocument::Compact).size() > m_limits.historyBytes) return false;
    const auto turns = value.value("turns").toArray();
    if (turns.size() > m_limits.historyTurns) return false;
    thread.binding = {id, m_account.identityFingerprint, m_initializeInfo.codexHome, {}, m_scratch, m_profileFingerprint};
    QSet<QString> ids;
    for (const auto& record : turns) {
        CodexTurn turn;
        if (!decodeTurn(record.toObject(), id, turn) || !terminal(turn.status) || ids.contains(turn.id)) return false;
        ids.insert(turn.id); thread.turns.append(turn);
    }
    return true;
}
void CodexClient::notification(const QJsonObject& object)
{
    const auto name = object.value("method").toString();
    if (!m_wire.contains("ServerNotification:" + name)) { ++m_ignored; return; }
    if (!m_initialized) { ++m_ignored; return; }
    if (!m_wire.accepts("ServerNotification:" + name, object)) { fatal(QStringLiteral("A required notification shape was invalid.")); return; }
    const auto params = object.value("params").toObject();
    if (name == "account/updated") {
        const auto mode = params.value("authMode");
        const bool chatGpt = mode == "chatgpt";
        if (!chatGpt && (!m_turnId.isEmpty() || hasPending(Operation::TurnStart))) {
            fatal(QStringLiteral("The account scope changed during an active turn."));
            return;
        }
        // This notification has no account identity. Token/plan refreshes must
        // reverify the fingerprint before invalidating a verified scope. Buffer
        // scoped traffic and block admission while that check is outstanding.
        m_accountRefreshPending = true;
        if (!chatGpt) {
            ++m_accountEpoch;
            m_account = {}; m_account.mode = mode.isNull() || mode.isUndefined() ? CodexAccountMode::SignedOut : CodexAccountMode::Unknown;
            emit accountChanged(m_account);
        }
        // An earlier account/read cannot restore stale readiness after this event.
        QList<qint64> obsolete;
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) if (it->operation == Operation::Account) obsolete.append(it.key());
        for (const auto id : obsolete) {
            m_pending.remove(id);
            emit requestFailed(id, method(Operation::Account), error(ErrorCode::OperationCancelled, QStringLiteral("The account check was superseded.")));
        }
        const auto refreshed = readAccount(); Q_UNUSED(refreshed);
        if (chatGpt && m_active) emit accountChanged(account());
    } else if (name == "account/login/completed") {
        if (hasPending(Operation::LoginStart)) {
            const auto bytes = QJsonDocument(params).toJson(QJsonDocument::Compact).size();
            if (m_earlyLogins.size() >= 8 || bytes > m_limits.earlyEventBytes - m_earlyLoginBytes) { fatal(QStringLiteral("Too many early login events.")); return; }
            m_earlyLogins.append(params); m_earlyLoginBytes += bytes;
        } else loginCompleted(params);
    } else if (name != "thread/started") turnEvent(object);
    else ++m_ignored; // Thread ownership comes from the correlated result only.
}
void CodexClient::turnEvent(const QJsonObject& object)
{
    const auto params = object.value("params").toObject();
    if (params.value("threadId").toString() != m_thread.threadId) { ++m_ignored; return; }
    if (!bindingMatches(m_thread)) {
        fatal(QStringLiteral("A turn event arrived after its account scope was revoked."));
        return;
    }
    if (hasPending(Operation::TurnStart)) {
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact).size();
        if (m_earlyTurns.size() >= m_limits.earlyEvents || bytes > m_limits.earlyEventBytes - m_earlyBytes) {
            fatal(QStringLiteral("The early event limit was exceeded.")); return;
        }
        m_earlyTurns.append(object); m_earlyBytes += bytes; return;
    }
    const auto name = object.value("method").toString();
    const auto turnId = name.startsWith("turn/") ? params.value("turn").toObject().value("id").toString() : params.value("turnId").toString();
    if (m_turnId.isEmpty() || turnId != m_turnId) { ++m_ignored; return; }
    if (name == "turn/started" || name == "turn/completed") {
        CodexTurn turn;
        if (!decodeTurn(params.value("turn").toObject(), m_thread.threadId, turn, true)
            || (name == "turn/completed" && !terminal(turn.status))) { fatal(QStringLiteral("A turn event was incompatible.")); return; }
        for (const auto& item : turn.items) if (!mergeItem(item)) { fatal(QStringLiteral("An item association was incompatible.")); return; }
        if (name == "turn/completed") finishTurn(turn);
    } else if (name == "item/agentMessage/delta") {
        const auto id = params.value("itemId").toString();
        auto item = m_items.value(id, CodexItem{m_thread.threadId, m_turnId, id, "agentMessage", {}, false, false});
        if (item.type != "agentMessage" || !stringId(id)) { fatal(QStringLiteral("An agent delta had the wrong item identity.")); return; }
        if (item.completed) { ++m_ignored; return; }
        item.text += params.value("delta").toString();
        if (!mergeItem(item)) fatal(QStringLiteral("The streamed item limit was exceeded."));
    } else {
        CodexItem item;
        if (!decodeItem(params.value("item").toObject(), m_thread.threadId, m_turnId, item)) { fatal(QStringLiteral("An item violated the text-only profile.")); return; }
        item.completed = name == "item/completed";
        if (!mergeItem(item)) fatal(QStringLiteral("An item association was incompatible."));
    }
}
bool CodexClient::mergeItem(CodexItem item)
{
    if (item.text.toUtf8().size() > m_limits.itemTextBytes || (m_items.size() >= m_limits.itemsPerTurn && !m_items.contains(item.id))) return false;
    const auto existing = m_items.constFind(item.id);
    if (existing != m_items.end()) {
        if (existing->type != item.type || existing->threadId != item.threadId || existing->turnId != item.turnId) return false;
        if (existing->completed) return !item.completed || existing->text == item.text;
        if (item.started && !item.completed && item.text.isEmpty()) item.text = existing->text;
        item.started = item.started || existing->started;
        if (existing->started == item.started && existing->completed == item.completed && existing->text == item.text) return true;
    }
    qsizetype total = item.text.toUtf8().size();
    for (auto it = m_items.begin(); it != m_items.end(); ++it) if (it.key() != item.id) total += it->text.toUtf8().size();
    if (total > m_limits.historyBytes) return false;
    if (!m_items.contains(item.id)) m_itemOrder.append(item.id);
    m_items.insert(item.id, item); emit itemChanged(item); return true;
}
void CodexClient::finishTurn(CodexTurn turn)
{
    if (m_turnId.isEmpty() || turn.id != m_turnId) { ++m_ignored; return; }
    m_lastTurnId = m_turnId; m_turnId.clear(); m_interruptAccepted = false;
    turn.items.clear();
    for (const auto& id : m_itemOrder) turn.items.append(m_items.value(id));
    if (turn.hasError && turn.error.code == ErrorCode::AuthenticationFailed) expireAccount();
    emit turnFinished(turn);
}
void CodexClient::serverRequest(const QJsonObject& object)
{
    const auto id = object.value("id");
    if (!id.isString() && (!id.isDouble() || !std::isfinite(id.toDouble()) || std::floor(id.toDouble()) != id.toDouble()
        || std::abs(id.toDouble()) > 9007199254740991.0)) { fatal(QStringLiteral("A server request ID was invalid.")); return; }
    const auto key = idKey(id);
    const auto hash = QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    const auto previous = m_serverRequests.constFind(key);
    if (previous != m_serverRequests.end()) {
        if (previous.value() != hash) fatal(QStringLiteral("A server request ID was reused inconsistently."));
        else ++m_ignored;
        return;
    }
    if (m_serverRequests.size() >= m_limits.serverRequests) { fatal(QStringLiteral("The server request limit was exceeded.")); return; }
    m_serverRequests.insert(key, hash);
    const auto name = object.value("method").toString();
    QJsonObject reply{{"id", id}};
    if (name == "item/commandExecution/requestApproval" || name == "item/fileChange/requestApproval") reply.insert("result", QJsonObject{{"decision", "decline"}});
    else if (name == "execCommandApproval" || name == "applyPatchApproval") reply.insert("result", QJsonObject{{"decision", "abort"}});
    else if (name == "item/permissions/requestApproval") reply.insert("result", QJsonObject{{"permissions", QJsonObject{}}, {"scope", "turn"}});
    else if (name == "item/tool/call") {
        const auto failureResponse = [this, &id](const QString& code) {
            return resolveDynamicToolCall(id, dynamicToolFailure(code));
        };
        const auto params = object.value(QStringLiteral("params")).toObject();
        if (!m_wire.accepts(QStringLiteral("ServerRequest:item/tool/call"), object)) {
            if (!failureResponse(QStringLiteral("invalid_request"))) fatal(QStringLiteral("A dynamic-tool rejection could not be queued."));
            return;
        }
        if (!m_dynamicToolsEnabled || !m_initialized || !m_profileVerified || !bindingMatches(m_thread)
            || (m_transport->kind() != CodexTransportKind::InMemoryTest && !liveInferenceGateOpen(true))
            || m_turnId.isEmpty() || params.value(QStringLiteral("threadId")).toString() != m_thread.threadId
            || params.value(QStringLiteral("turnId")).toString() != m_turnId
            || params.value(QStringLiteral("callId")).toString().isEmpty()
            || !params.value(QStringLiteral("arguments")).isObject()) {
            if (!failureResponse(QStringLiteral("scope_denied"))) fatal(QStringLiteral("A dynamic-tool denial could not be queued."));
            return;
        }
        emit dynamicToolCallReceived(id, params, m_generation);
        return;
    }
    else reply.insert("error", QJsonObject{{"code", -32601}, {"message", "This server method is not supported by DevHub."}});
    const auto sent = m_transport->sendObject(reply);
    if (!sent) fatal(QStringLiteral("A server-request denial could not be queued."));
    else if (name == QStringLiteral("item/commandExecution/requestApproval")
        || name == QStringLiteral("item/fileChange/requestApproval")
        || name == QStringLiteral("execCommandApproval") || name == QStringLiteral("applyPatchApproval")
        || name == QStringLiteral("item/permissions/requestApproval")) m_resolvedServerRequests.insert(key);
}
bool CodexClient::resolveDynamicToolCall(const QJsonValue& requestId, const QJsonObject& response)
{
    if (!m_active || !m_transport || ( !requestId.isString() && !requestId.isDouble())) return false;
    const QByteArray key = idKey(requestId);
    if (!m_serverRequests.contains(key) || m_resolvedServerRequests.contains(key)) return false;
    if (!m_wire.accepts(QStringLiteral("Response:DynamicToolCallResponse"), response)) return false;
    const auto scopedResponse = !m_accountRefreshPending && (!bindingMatches(m_thread)
        || (m_transport->kind() != CodexTransportKind::InMemoryTest && !liveInferenceGateOpen(true)))
        ? dynamicToolFailure(QStringLiteral("scope_denied")) : response;
    const QJsonObject envelope{{QStringLiteral("id"), requestId}, {QStringLiteral("result"), scopedResponse}};
    if (m_accountRefreshPending) {
        for (const auto& deferred : m_accountDeferred)
            if (deferred.first && idKey(deferred.second.value("id")) == key) return false;
        const auto bytes = QJsonDocument(envelope).toJson(QJsonDocument::Compact).size();
        if (m_accountDeferred.size() >= m_limits.earlyEvents || bytes > m_limits.earlyEventBytes - m_accountDeferredBytes) {
            fatal(QStringLiteral("The account verification event limit was exceeded.")); return false;
        }
        m_accountDeferred.append({true, envelope}); m_accountDeferredBytes += bytes; return true;
    }
    m_resolvedServerRequests.insert(key);
    if (!m_transport->sendObject(envelope)) {
        fatal(QStringLiteral("A dynamic-tool response could not be queued."));
        return false;
    }
    return true;
}
void CodexClient::loginCompleted(const QJsonObject& params)
{
    if (!m_login || params.value("loginId").toString() != m_login->loginId) { ++m_ignored; return; }
    const auto id = m_login->loginId; const bool success = params.value("success").toBool();
    clearLogin(); emit loginFinished(id, success);
    const auto refreshed = readAccount(); Q_UNUSED(refreshed);
}
void CodexClient::clearLogin()
{
    m_loginTimer.stop(); m_login.reset(); m_earlyLogins.clear(); m_earlyLoginBytes = 0; emit loginCleared();
}
void CodexClient::expireAccount()
{
    ++m_accountEpoch; m_account = {}; m_account.mode = CodexAccountMode::Expired; emit accountChanged(m_account);
}
void CodexClient::failPending(qint64 id, const Pending& pending, AppError failure, bool uncertain)
{
    if (pending.generation != m_generation) { emit requestFailed(id, method(pending.operation), failure); return; }
    const bool profileFailure = pending.operation == Operation::Config || pending.operation == Operation::Requirements
        || pending.operation == Operation::Features || pending.operation == Operation::Hooks
        || pending.operation == Operation::Mcp || pending.operation == Operation::Plugins;
    const bool loginFailure = pending.operation == Operation::LoginStart;
    const bool accountFailure = pending.operation == Operation::Account && pending.accountEpoch == m_accountEpoch && m_account.mode != CodexAccountMode::Expired;
    const bool refreshFailure = accountFailure && m_accountRefreshPending;
    if (profileFailure) {
        CodexBehaviorVerifier::forgetCachedEvidence(m_compatibility);
        m_profileVerified = false; m_config.reset(); m_requirements.reset();
        m_sessionBehaviorEvidence.clear();
        m_compatibility.runtimePolicyVerified = false;
        m_compatibility.state = CodexCompatibilityState::VerificationRequired;
        m_compatibility.reason = CodexCompatibilityReason::RuntimeVerificationRequired;
        m_compatibility.explanation = "A required runtime restriction check did not complete. Reconnect to retry.";
        emit compatibilityChanged(m_compatibility);
    }
    if (loginFailure) { m_loginTimer.stop(); m_login.reset(); m_earlyLogins.clear(); m_earlyLoginBytes = 0; }
    if (accountFailure) m_account = {};
    if (pending.operation == Operation::TurnStart || pending.operation == Operation::ThreadStart || pending.operation == Operation::ThreadResume) {
        m_uncertain = m_uncertain || uncertain || (pending.operation == Operation::TurnStart && !m_earlyTurns.isEmpty());
        m_earlyTurns.clear(); m_earlyBytes = 0;
    }
    const auto account = m_account;
    if (profileFailure) emit profileChecked(false, {QStringLiteral("profile-check-failed")});
    if (loginFailure) emit loginCleared();
    if (accountFailure) emit accountChanged(account);
    emit requestFailed(id, method(pending.operation), failure);
    if (refreshFailure) fatal(QStringLiteral("The Codex account could not be verified after an update."), pending.generation);
}
void CodexClient::disconnectClient(AppError failure)
{
    const auto generation = m_generation;
    m_active = false; m_initialized = false; m_profileVerified = false;
    m_behaviorVerifier.cancel();
    m_compatibility.runtimePolicyVerified = false;
    m_compatibility.state = CodexCompatibilityState::VerificationRequired;
    m_compatibility.reason = CodexCompatibilityReason::EvidenceMissing;
    m_compatibility.explanation = "Reconnect to revalidate runtime restrictions.";
    m_developmentLiveValidationAuthorized = false; m_deadlineTimer.stop();
    const auto pending = std::exchange(m_pending, {});
    m_config.reset(); m_requirements.reset(); m_earlyTurns.clear(); m_earlyBytes = 0;
    m_accountDeferred.clear(); m_accountDeferredBytes = 0; m_accountRefreshPending = false;
    m_loginTimer.stop(); m_login.reset(); m_earlyLogins.clear(); m_earlyLoginBytes = 0;
    m_account = {}; ++m_accountEpoch;
    std::optional<CodexTurn> interrupted;
    if (!m_turnId.isEmpty()) {
        interrupted = CodexTurn{m_thread.threadId, m_turnId, CodexTurnStatus::Uncertain, {}, failure, true};
        for (const auto& id : m_itemOrder) interrupted->items.append(m_items.value(id));
        m_lastTurnId = m_turnId; m_turnId.clear(); m_interruptAccepted = false;
    }
    // Complete all state changes before callbacks can reconnect this client.
    emit compatibilityChanged(m_compatibility); emit loginCleared(); emit accountChanged(CodexAccount{});
    for (auto it = pending.begin(); it != pending.end(); ++it) emit requestFailed(it.key(), method(it->operation), failure);
    if (interrupted) emit turnFinished(*interrupted);
    emit connectionClosed(generation);
}
void CodexClient::fatal(const QString& safeReason, quint64 expectedGeneration)
{
    if (!m_active || (expectedGeneration && expectedGeneration != m_generation)) return;
    const auto generation = m_generation;
    const auto failure = error(ErrorCode::CodexProtocolFailure, safeReason);
    disconnectClient(failure); emit protocolFailed(failure);
    if (m_transport && m_generation == generation && m_transportBusy) m_transport->stop();
}
void CodexClient::deadlines()
{
    QList<qint64> expired;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) if (it->deadline <= m_clock.elapsed()) expired.append(it.key());
    for (const auto id : expired) {
        const auto found = m_pending.find(id); if (found == m_pending.end()) continue;
        const auto pending = found.value(); m_pending.erase(found);
        failPending(id, pending, error(ErrorCode::CodexRequestTimeout, QStringLiteral("The Codex acknowledgement timed out.")), true);
        if (pending.operation == Operation::Initialize || pending.operation == Operation::LoginStart)
            fatal(QStringLiteral("Codex startup or login became uncertain."), pending.generation);
    }
}
}
