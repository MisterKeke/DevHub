#include "CodexClient.h"
#include "CodexRuntimeProfile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
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
QByteArray idKey(const QJsonValue& id) { return QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact); }
bool stringId(const QString& id) { return !id.isEmpty() && id.size() <= 512; }
bool terminal(CodexTurnStatus status) { return status != CodexTurnStatus::InProgress; }
bool safeTextItem(const QString& type) { return type == "agentMessage" || type == "userMessage" || type == "reasoning" || type == "plan" || type == "contextCompaction"; }
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
            {"capabilities", QJsonObject{{"experimentalApi", true}}}});
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
Result<void> CodexClient::launch(const CodexLaunchSpec& spec)
{
    if (!m_transport || m_active || m_transportBusy || spec.processGeneration <= m_generation || spec.executable.version != "0.160.1"
        || !QFileInfo(spec.scratchDirectory).isAbsolute() || !QFileInfo(spec.scratchDirectory).isDir()
        || !CodexRuntimeProfile::acceptsEnvironment(spec.environment) || !CodexRuntimeProfile::acceptsLaunch(spec.arguments)) return Result<void>::failure(denied());
    m_generation = spec.processGeneration; m_active = true; m_initialized = false; m_profileVerified = false;
    m_transportBusy = true; m_initializeSent = false;
    m_uncertain = false; m_interruptAccepted = false; m_scratch = QFileInfo(spec.scratchDirectory).canonicalFilePath();
    m_profileFingerprint = CodexRuntimeProfile::fingerprint(spec.arguments);
    m_expectedHome.clear();
    for (const auto& key : spec.environment.keys()) if (key.compare("CODEX_HOME", Qt::CaseInsensitive) == 0)
        m_expectedHome = QFileInfo(spec.environment.value(key)).canonicalFilePath();
    m_initializeInfo = {}; m_account = {}; m_thread = {}; m_turnId.clear(); m_lastTurnId.clear();
    m_items.clear(); m_itemOrder.clear(); m_earlyTurns.clear(); m_earlyLogins.clear(); m_earlyBytes = 0; m_earlyLoginBytes = 0; m_serverRequests.clear();
    m_config.reset(); m_requirements.reset(); ++m_accountEpoch;
    m_deadlineTimer.start(); m_transport->launch(spec);
    return Result<void>::success();
}
void CodexClient::stop()
{
    const auto generation = m_generation;
    if (m_active) disconnectClient(error(ErrorCode::OperationCancelled, QStringLiteral("The Codex connection is stopping.")));
    if (m_transport && m_transportBusy && m_generation == generation) m_transport->stop();
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
    if (m_login || hasPending(Operation::LoginStart) || hasPending(Operation::LoginCancel) || !m_turnId.isEmpty()
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
    if (!m_initialized || hasPending(Operation::Config) || hasPending(Operation::Requirements) || !m_turnId.isEmpty()
        || hasPending(Operation::TurnStart) || hasPending(Operation::ThreadStart) || hasPending(Operation::ThreadResume)) return Result<void>::failure(denied());
    m_profileVerified = false; m_config.reset(); m_requirements.reset();
    const auto config = request(Operation::Config, QJsonObject{{"includeLayers", true}, {"cwd", m_scratch}});
    if (!config) return Result<void>::failure(config.error());
    const auto requirements = request(Operation::Requirements, QJsonValue(QJsonValue::Null));
    if (!requirements) return Result<void>::failure(requirements.error());
    return Result<void>::success();
}
bool CodexClient::bindingMatches(const CodexThreadBinding& binding) const
{
    return m_initialized && m_profileVerified && stringId(binding.threadId) && m_account.mode == CodexAccountMode::ChatGpt
        && m_account.requiresOpenaiAuth && !m_account.identityFingerprint.isEmpty()
        && binding.accountFingerprint == m_account.identityFingerprint && binding.codexHome == m_initializeInfo.codexHome
        && binding.scratchDirectory == m_scratch && binding.profileFingerprint == m_profileFingerprint;
}
Result<qint64> CodexClient::startThread(const QString& model)
{
    if (!m_initialized || !m_profileVerified || m_account.mode != CodexAccountMode::ChatGpt || !m_account.requiresOpenaiAuth
        || m_account.identityFingerprint.isEmpty() || hasPending(Operation::Account) || !stringId(model) || !m_turnId.isEmpty() || m_uncertain
        || hasPending(Operation::TurnStart) || hasPending(Operation::ThreadResume) || hasPending(Operation::ThreadRead)
        || m_login || hasPending(Operation::LoginStart)) return Result<qint64>::failure(denied());
    return request(Operation::ThreadStart, QJsonObject{{"cwd", m_scratch}, {"model", model}, {"sandbox", "read-only"},
        {"approvalPolicy", "on-request"}, {"historyMode", "legacy"}, {"environments", QJsonArray{}},
        {"runtimeWorkspaceRoots", QJsonArray{}}, {"selectedCapabilityRoots", QJsonArray{}}, {"dynamicTools", QJsonArray{}},
        {"allowProviderModelFallback", false}});
}
Result<qint64> CodexClient::resumeThread(const CodexThreadBinding& binding)
{
    if (!bindingMatches(binding) || m_uncertain || !m_turnId.isEmpty() || hasPending(Operation::TurnStart)
        || hasPending(Operation::ThreadStart) || hasPending(Operation::ThreadRead) || hasPending(Operation::Account)
        || m_login || hasPending(Operation::LoginStart)) return Result<qint64>::failure(denied());
    // Resume has no environments/dynamicTools parameters in the pinned schema.
    // Validate returned environments and send [] again on every admitted turn.
    return request(Operation::ThreadResume, QJsonObject{{"threadId", binding.threadId}, {"cwd", m_scratch},
        {"sandbox", "read-only"}, {"approvalPolicy", "on-request"}, {"runtimeWorkspaceRoots", QJsonArray{}}}, binding.threadId);
}
Result<qint64> CodexClient::readThread(const CodexThreadBinding& binding)
{
    if (!bindingMatches(binding) || !m_turnId.isEmpty() || hasPending(Operation::TurnStart)
        || hasPending(Operation::ThreadStart) || hasPending(Operation::ThreadResume)) return Result<qint64>::failure(denied());
    return request(Operation::ThreadRead, QJsonObject{{"threadId", binding.threadId}, {"includeTurns", true}}, binding.threadId);
}
bool CodexClient::canSend() const
{
    return m_active && bindingMatches(m_thread) && !m_uncertain && m_turnId.isEmpty() && !hasPending(Operation::TurnStart)
        && !hasPending(Operation::ThreadStart) && !hasPending(Operation::ThreadResume) && !hasPending(Operation::ThreadRead) && !m_login
        && !hasPending(Operation::LoginStart) && !hasPending(Operation::Account) && m_transport
        && (m_transport->kind() == CodexTransportKind::InMemoryTest || CodexRuntimeProfile::liveInferenceApproved());
}
bool CodexClient::canStartConversation() const
{
    return m_active && m_initialized && m_profileVerified && !m_uncertain && m_turnId.isEmpty()
        && m_account.mode == CodexAccountMode::ChatGpt && m_account.requiresOpenaiAuth
        && !m_account.identityFingerprint.isEmpty() && !hasPending(Operation::Account)
        && !hasPending(Operation::ThreadStart) && !hasPending(Operation::ThreadResume)
        && !hasPending(Operation::ThreadRead) && !hasPending(Operation::TurnStart)
        && !m_login && !hasPending(Operation::LoginStart) && m_transport
        && (m_transport->kind() == CodexTransportKind::InMemoryTest || CodexRuntimeProfile::liveInferenceApproved());
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
    if (object.contains("method")) {
        if (object.contains("id")) serverRequest(object);
        else notification(object);
    } else response(object);
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
        failPending(id, pending, safeError, false); return;
    }
    const auto result = object.value("result");
    if (!m_wire.accepts(responseContract(pending.operation), result) || !result.isObject()) {
        failPending(id, pending, invalidResponse(), true); fatal(QStringLiteral("A required response shape was invalid."), pending.generation); return;
    }
    if (!decodeResult(id, pending, result.toObject())) {
        failPending(id, pending, invalidResponse(), true); fatal(QStringLiteral("A response violated the active scope."), pending.generation); return;
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
        if (m_account.mode != account.mode || m_account.identityFingerprint != account.identityFingerprint) {
            ++m_accountEpoch;
            if (!m_turnId.isEmpty()) {
                fatal(QStringLiteral("The account scope changed during an active turn."), pending.generation);
                return false;
            }
        }
        m_account = account; emit accountChanged(m_account); return true;
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
    case Operation::ThreadStart:
    case Operation::ThreadResume:
    case Operation::ThreadRead: {
        if (pending.accountEpoch != m_accountEpoch) return false;
        CodexThread thread;
        if (!decodeThread(result.value("thread").toObject(), thread)) return false;
        if (!pending.expectedThread.isEmpty() && thread.binding.threadId != pending.expectedThread) return false;
        if (pending.operation != Operation::ThreadRead) {
            if (QDir::cleanPath(result.value("cwd").toString()) != m_scratch || result.value("modelProvider") != "openai"
                || result.value("approvalPolicy") != "on-request" || result.value("sandbox").toObject().value("type") != "readOnly"
                || result.value("sandbox").toObject().value("networkAccess").toBool()
                || !result.value("runtimeWorkspaceRoots").toArray().isEmpty()) return false;
            if (pending.operation == Operation::ThreadResume && (!result.value("initialTurnsPage").isNull() && !result.value("initialTurnsPage").isUndefined())) return false;
            if (!result.value("turnsBackwardsCursor").toString().isEmpty() || !result.value("itemsBackwardsCursor").toString().isEmpty()) return false;
            m_thread = thread.binding; m_lastTurnId.clear();
        }
        emit threadReceived(thread); return true;
    }
    case Operation::TurnStart: {
        if (pending.accountEpoch != m_accountEpoch || pending.expectedThread != m_thread.threadId) return false;
        CodexTurn turn;
        if (!decodeTurn(result.value("turn").toObject(), pending.expectedThread, turn) || turn.id == m_lastTurnId) return false;
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
    const auto reasons = CodexRuntimeProfile::rejectionReasons(*m_config, *m_requirements);
    m_config.reset(); m_requirements.reset(); m_profileVerified = reasons.isEmpty();
    emit profileChecked(m_profileVerified, reasons);
}
bool CodexClient::decodeItem(const QJsonObject& value, const QString& threadId, const QString& turnId, CodexItem& item) const
{
    item = {threadId, turnId, value.value("id").toString(), value.value("type").toString(), {}, true, false};
    if (!stringId(item.id) || !safeTextItem(item.type)) return false;
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
bool CodexClient::decodeTurn(const QJsonObject& value, const QString& threadId, CodexTurn& turn) const
{
    turn.threadId = threadId; turn.id = value.value("id").toString();
    if (!stringId(turn.id) || value.value("itemsView").toString("full") != "full") return false;
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
    const auto items = value.value("items").toArray();
    if (items.size() > m_limits.itemsPerTurn) return false;
    QSet<QString> ids;
    for (const auto& record : items) {
        CodexItem item;
        if (!decodeItem(record.toObject(), threadId, turn.id, item) || ids.contains(item.id)) return false;
        ids.insert(item.id); item.completed = terminal(turn.status); turn.items.append(item);
    }
    return true;
}
bool CodexClient::decodeThread(const QJsonObject& value, CodexThread& thread) const
{
    const auto id = value.value("id").toString();
    const auto environments = value.value("environments");
    if (!stringId(id) || QDir::cleanPath(value.value("cwd").toString()) != m_scratch
        || value.value("modelProvider") != "openai" || value.value("historyMode").toString("legacy") != "legacy"
        || !environments.isArray() || !environments.toArray().isEmpty()
        || QJsonDocument(value).toJson(QJsonDocument::Compact).size() > m_limits.historyBytes) return false;
    const auto turns = value.value("turns").toArray();
    if (turns.size() > m_limits.historyTurns) return false;
    thread.binding = {id, m_account.identityFingerprint, m_initializeInfo.codexHome, m_scratch, m_profileFingerprint};
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
        if (!m_turnId.isEmpty()) {
            fatal(QStringLiteral("The account scope changed during an active turn."));
            return;
        }
        ++m_accountEpoch;
        const auto mode = params.value("authMode");
        m_account = {}; m_account.mode = mode.isNull() || mode.isUndefined() ? CodexAccountMode::SignedOut : CodexAccountMode::Unknown;
        emit accountChanged(m_account);
        // An earlier account/read cannot restore stale readiness after this event.
        QList<qint64> obsolete;
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) if (it->operation == Operation::Account) obsolete.append(it.key());
        for (const auto id : obsolete) {
            const auto pending = m_pending.take(id);
            failPending(id, pending, error(ErrorCode::OperationCancelled, QStringLiteral("The account state changed.")), false);
        }
        const auto refreshed = readAccount(); Q_UNUSED(refreshed);
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
        if (!decodeTurn(params.value("turn").toObject(), m_thread.threadId, turn)
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
    else if (name == "item/tool/call") reply.insert("result", QJsonObject{{"success", false}, {"contentItems", QJsonArray{
        QJsonObject{{"type", "inputText"}, {"text", "This capability is disabled in DevHub."}}}}});
    else reply.insert("error", QJsonObject{{"code", -32601}, {"message", "This server method is not supported by DevHub."}});
    const auto sent = m_transport->sendObject(reply);
    if (!sent) fatal(QStringLiteral("A server-request denial could not be queued."));
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
    const bool profileFailure = pending.operation == Operation::Config || pending.operation == Operation::Requirements;
    const bool loginFailure = pending.operation == Operation::LoginStart;
    const bool accountFailure = pending.operation == Operation::Account && pending.accountEpoch == m_accountEpoch && m_account.mode != CodexAccountMode::Expired;
    if (profileFailure) {
        m_profileVerified = false; m_config.reset(); m_requirements.reset();
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
}
void CodexClient::disconnectClient(AppError failure)
{
    const auto generation = m_generation;
    m_active = false; m_initialized = false; m_profileVerified = false; m_deadlineTimer.stop();
    const auto pending = std::exchange(m_pending, {});
    m_config.reset(); m_requirements.reset(); m_earlyTurns.clear(); m_earlyBytes = 0;
    m_loginTimer.stop(); m_login.reset(); m_earlyLogins.clear(); m_earlyLoginBytes = 0;
    m_account = {}; ++m_accountEpoch;
    std::optional<CodexTurn> interrupted;
    if (!m_turnId.isEmpty()) {
        interrupted = CodexTurn{m_thread.threadId, m_turnId, CodexTurnStatus::Uncertain, {}, failure, true};
        for (const auto& id : m_itemOrder) interrupted->items.append(m_items.value(id));
        m_lastTurnId = m_turnId; m_turnId.clear(); m_interruptAccepted = false;
    }
    // Complete all state changes before callbacks can reconnect this client.
    emit loginCleared(); emit accountChanged(CodexAccount{});
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
