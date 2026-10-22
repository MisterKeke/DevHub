#include "AssistantController.h"
#include "AssistantApprovalCoordinator.h"
#include "CodexLoginPolicy.h"
#include <algorithm>
#include <QDir>
#include <utility>

namespace devhub::application::assistant {
void AssistantController::setApprovalCoordinator(AssistantApprovalCoordinator* coordinator) {
    if (m_approvals == coordinator) return;
    if (m_approvals) disconnect(m_approvals, nullptr, this, nullptr);
    m_approvals = coordinator;
    if (coordinator) connect(coordinator, &AssistantApprovalCoordinator::changed, this, &AssistantController::approvalCoordinatorChanged);
    emit approvalCoordinatorChanged();
}
using domain::errors::ErrorCode;
namespace {
bool sameContextBinding(const AssistantContext& left, const AssistantContext& right)
{
    return left.workspaceId == right.workspaceId
        && QString::compare(QDir::cleanPath(left.canonicalRoot), QDir::cleanPath(right.canonicalRoot), Qt::CaseInsensitive) == 0;
}
QStringList profileProblems(const QStringList& reasons)
{
    static const QHash<QString, QString> featureLabels{
        {"shell_tool", QObject::tr("command execution")}, {"unified_exec", QObject::tr("resumable command execution")},
        {"apps", QObject::tr("Codex apps")}, {"plugins", QObject::tr("Codex plugins")}, {"hooks", QObject::tr("Codex hooks")},
        {"browser_use", QObject::tr("browser control")}, {"computer_use", QObject::tr("computer control")},
        {"goals", QObject::tr("background goals")}, {"image_generation", QObject::tr("image generation")},
        {"multi_agent", QObject::tr("multiple agents")}, {"js_repl", QObject::tr("JavaScript execution")},
        {"apply_patch_freeform", QObject::tr("native file editing")}, {"shell_snapshot", QObject::tr("shell environment snapshots")}};
    QStringList problems;
    // Explain only known checks; never display raw runtime/configuration values.
    for (const auto& reason : reasons) {
        QString problem;
        if (reason == "inherited-mcp-enabled") problem = QObject::tr("inherited MCP servers are enabled");
        else if (reason == "legacy-notify-enabled") problem = QObject::tr("an inherited notification command is enabled");
        else if (reason == "unverified-attached-runtime") problem = QObject::tr("attached desktop/browser settings are present");
        else if (reason == "unverified-provider-overrides" || reason == "unsupported-auth-or-provider") problem = QObject::tr("the authentication or model provider settings are unsupported");
        else if (reason == "unverified-config-source") problem = QObject::tr("a configuration source is unverified");
        else if (reason == "unverified-managed-requirements" || reason == "invalid-managed-requirements") problem = QObject::tr("managed requirements are unsupported");
        else if (reason.startsWith("feature-not-disabled:") || reason.startsWith("native-feature-enabled:")) {
            const auto label = featureLabels.value(reason.section(':', 1));
            problem = label.isEmpty() ? QObject::tr("a native Codex feature could not be verified as disabled")
                : QObject::tr("%1 is enabled in Codex").arg(label);
        }
        else if (reason == "web-search-not-disabled" || reason == "unsafe-default-policy") problem = QObject::tr("native capabilities or policies do not meet the restrictions");
        else if (reason == "feature-inventory-incomplete" || reason == "feature-inventory-unverified") problem = QObject::tr("Codex did not return a complete, valid feature inventory");
        else if (reason == "configuration-watch-unavailable") problem = QObject::tr("Codex configuration files could not be monitored for changes");
        else if (reason == "hook-enabled" || reason == "hooks-inventory-unverified") problem = QObject::tr("Codex hooks are active or could not be verified as disabled");
        else if (reason == "plugin-enabled" || reason == "plugin-inventory-unverified") problem = QObject::tr("Codex plugins are active or could not be verified as disabled");
        else if (reason == "inherited-mcp-active") problem = QObject::tr("an inherited MCP server is still active");
        else if (reason == "mcp-inventory-unverified") problem = QObject::tr("inherited MCP server restrictions could not be verified");
        else if (reason == "unverified-capability-roots") problem = QObject::tr("additional workspace access is configured");
        else problem = QObject::tr("a profile restriction could not be verified");
        if (!problems.contains(problem)) problems.append(problem);
    }
    return problems;
}
}
AssistantController::AssistantController(ICodexClient& client, shared::time::IApplicationClock& clock,
    shared::identifiers::IIdGenerator& ids, QString scratchDirectory, QStringList launchArguments,
    QProcessEnvironment environment, IConversationRepository* repository, QObject* parent, AssistantLimits limits)
    : QObject(parent), m_client(client), m_clock(clock), m_ids(ids), m_repository(repository),
      m_scratch(std::move(scratchDirectory)), m_arguments(std::move(launchArguments)), m_baseArguments(m_arguments),
      m_environment(std::move(environment)), m_limits(limits)
{
    resetConversation();
    connect(&client, &ICodexClient::compatibilityChanged, this, [this] {
        if (m_client.compatibilityReport().runtimePolicyVerified) maybeRestoreConversation();
        emit stateChanged();
    });
    m_renderTimer.setSingleShot(true); m_renderTimer.setInterval(m_limits.renderMilliseconds);
    m_cancelTimer.setSingleShot(true); m_cancelTimer.setInterval(m_limits.cancellationMilliseconds);
    m_startupTimer.setSingleShot(true); m_startupTimer.setInterval(m_limits.startupMilliseconds);
    connect(&m_startupTimer, &QTimer::timeout, this, [this] {
        if (m_connection != AssistantConnectionState::Starting) return;
        m_connection = AssistantConnectionState::Failed;
        m_client.stop();
        setError(ErrorCode::StartupFailed, tr("Codex startup timed out. Reconnect to try again."));
    });
    connect(&m_renderTimer, &QTimer::timeout, this, &AssistantController::flushMessages);
    connect(&m_cancelTimer, &QTimer::timeout, this, [this] {
        m_client.stop();
        if (!m_backendOwned && m_shuttingDown) completeShutdown();
    });
    connect(&client, &ICodexClient::initialized, this, [this] {
        m_startupTimer.stop();
        m_modelsRequested = true;
        const auto result = m_client.listModels();
        if (!result) setError(result.error().code, result.error().userMessage);
        refreshConnection();
    });
    connect(&client, &ICodexClient::modelsReceived, this, [this](const CodexModelPage& page) {
        m_models = page.models; selectModel();
        if (m_loginStatus == tr("ChatGPT sign-in verified. Checking available models…")) m_loginStatus = tr("ChatGPT sign-in verified.");
        maybeRestoreConversation();
        refreshConnection();
    });
    connect(&client, &ICodexClient::loginChallenge, this, [this](const CodexLoginChallenge& challenge) {
        if (!m_allowed || !m_loginPending || !acceptsCodexLoginUrl(challenge.url, challenge.mode == CodexLoginMode::DeviceCode)) return;
        m_challenge = challenge; m_loginPending = false; m_loginStatus = tr("Complete sign-in on the validated authorization page."); emit stateChanged();
    });
    connect(&client, &ICodexClient::loginCleared, this, [this] { m_challenge.reset(); emit stateChanged(); });
    connect(&client, &ICodexClient::loginFinished, this, [this](const QString&, bool success) {
        m_loginPending = false; m_challenge.reset();
        m_refreshModelsAfterLogin = success;
        m_loginStatus = success ? tr("Sign-in completed. Checking the Codex account…") : tr("Sign-in cancelled or not completed.");
        emit stateChanged(); QTimer::singleShot(0, this, &AssistantController::applyPendingConfiguration);
    });
    connect(&client, &ICodexClient::accountChanged, this, [this] {
        if (m_refreshModelsAfterLogin && m_allowed && m_client.isInitialized() && m_client.account().mode == CodexAccountMode::ChatGpt) {
            m_refreshModelsAfterLogin = false; m_models.clear(); m_model.clear(); m_loginStatus = tr("ChatGPT sign-in verified. Checking available models…");
            const auto result = m_client.listModels(); if (!result) setError(result.error().code, result.error().userMessage);
        }
        maybeRestoreConversation();
        refreshConnection();
    });
    connect(&client, &ICodexClient::profileChecked, this, [this](bool accepted, const QStringList& reasons) {
        m_profileProblems = accepted ? QStringList{} : profileProblems(reasons);
        if (!accepted) setError(ErrorCode::AssistantPermissionDenied,
            tr("Codex restriction check failed: %1. Reconnect after resolving this check.").arg(m_profileProblems.join(QStringLiteral("; "))));
        else maybeRestoreConversation();
        refreshConnection();
    });
    connect(&client, &ICodexClient::restrictedLaunchRequired, this,
        [this](const QStringList& arguments, quint64 generation) {
            if (generation != m_processGeneration || m_profileRetryAttempted || m_client.profileVerified()
                || !canReconnect() || arguments == m_arguments) return;
            m_profileRetryAttempted = true; m_arguments = arguments;
            restartConnection();
        }, Qt::QueuedConnection);
    connect(&client, &ICodexClient::threadReceived, this, &AssistantController::receivedThread);
    connect(&client, &ICodexClient::turnAccepted, this, [this](const QString& threadId, const QString& turnId) {
        if (m_operation != AssistantOperation::Submitting && m_operation != AssistantOperation::Cancelling) return;
        if (auto* user = mutableMessage(m_pendingUser)) {
            user->delivery = AssistantDelivery::Accepted; user->threadId = threadId; user->turnId = turnId; emit messageChanged(user->id);
        }
        persistCurrent(QStringLiteral("accepted"));
        if (m_operation == AssistantOperation::Cancelling) {
            const auto interrupted = m_client.interruptTurn(); Q_UNUSED(interrupted);
        } else m_operation = AssistantOperation::Streaming;
        emit stateChanged();
    });
    connect(&client, &ICodexClient::itemChanged, this, &AssistantController::receivedItem);
    connect(&client, &ICodexClient::turnFinished, this, &AssistantController::finishedTurn);
    connect(&client, &ICodexClient::requestFailed, this, [this](qint64, const QString& method, const domain::errors::AppError& error) { failedRequest(method, error); });
    connect(&client, &ICodexClient::protocolFailed, this, [this](const domain::errors::AppError& error) {
        if (!m_shuttingDown && !m_reconnectWanted) m_connection = AssistantConnectionState::Failed;
        setError(error.code, error.userMessage);
    });
    connect(&client, &ICodexClient::connectionClosed, this, [this](quint64 generation) {
        if (generation != m_processGeneration) return;
        m_startupTimer.stop();
        m_loginPending = false; m_challenge.reset();
        if (m_connection == AssistantConnectionState::Starting) {
            m_connection = AssistantConnectionState::Failed;
            if (m_error.userMessage.isEmpty()) setError(ErrorCode::StartupFailed, tr("Codex stopped before startup completed. Reconnect to try again."));
        } else if (m_connection != AssistantConnectionState::Failed)
            m_connection = m_reconnectWanted ? AssistantConnectionState::Stopping : AssistantConnectionState::Disconnected;
        if (m_operation == AssistantOperation::Restoring) {
            m_operation = AssistantOperation::Idle; m_threadOwned = false; m_restoreMetadata.reset();
            m_restoreUnavailable = true;
            setError(ErrorCode::AssistantHistoryUnavailable, tr("The saved conversation could not be reached. Start a new chat."));
        } else if (busy()) {
            if (auto* user = mutableMessage(m_pendingUser)) { user->delivery = AssistantDelivery::Uncertain; user->final = true; emit messageChanged(user->id); }
            m_operation = AssistantOperation::Uncertain;
            persistCurrent(QStringLiteral("uncertain"));
        }
        emit stateChanged();
    });
    connect(&client, &ICodexClient::backendStopped, this, [this](quint64 generation) {
        if (generation != m_processGeneration) return;
        m_backendOwned = false; m_cancelTimer.stop();
        if (m_shuttingDown) { completeShutdown(); return; }
        if (m_newChatWanted) { m_newChatWanted = false; resetConversation(); }
        if (std::exchange(m_reconnectWanted, false) && m_allowed) beginProbe();
        else { if (m_connection != AssistantConnectionState::Failed) m_connection = AssistantConnectionState::Disconnected; emit stateChanged(); }
        QTimer::singleShot(0, this, &AssistantController::applyPendingConfiguration);
    });
}
void AssistantController::open()
{
    m_opened = true;
    if (m_allowed && m_connection == AssistantConnectionState::Disconnected && !m_backendOwned) beginProbe();
}
void AssistantController::beginProbe()
{
    m_restoreAttempted = false;
    m_connection = AssistantConnectionState::Probing; m_modelsRequested = false; m_model.clear(); m_models.clear(); m_runtimeVersion.clear(); m_error = {};
    m_profileProblems.clear();
    m_loginStatus.clear(); m_refreshModelsAfterLogin = false;
    emit stateChanged(); emit executableProbeRequested(m_configuration.executablePath, ++m_probeGeneration);
}
void AssistantController::reconnect()
{
    if (!canReconnect()) return;
    m_profileRetryAttempted = false; m_arguments = m_baseArguments;
    restartConnection();
}
void AssistantController::restartConnection()
{
    m_opened = true;
    m_startupTimer.stop();
    ++m_probeGeneration; emit cancelProbeRequested();
    if (m_backendOwned) {
        m_reconnectWanted = true; m_connection = AssistantConnectionState::Stopping; emit stateChanged(); m_client.stop();
    } else beginProbe();
}
void AssistantController::executableFound(CodexExecutable executable, quint64 generation)
{
    if (!m_allowed || generation != m_probeGeneration || m_connection != AssistantConnectionState::Probing) return;
    m_runtimeVersion = executable.version;
    m_startupTimer.start();
    m_backendOwned = true; m_connection = AssistantConnectionState::Starting; emit stateChanged();
    const auto result = m_client.launch({std::move(executable), m_arguments, m_scratch, m_environment, ++m_processGeneration});
    if (!result) { m_startupTimer.stop(); m_backendOwned = false; m_connection = AssistantConnectionState::Failed; setError(result.error().code, result.error().userMessage); }
}
void AssistantController::probeFailed(domain::errors::AppError error, quint64 generation)
{
    if (generation != m_probeGeneration || !m_allowed) return;
    m_connection = AssistantConnectionState::Failed; setError(error.code, error.userMessage);
}
void AssistantController::refreshConnection()
{
    if (!m_allowed || m_connection == AssistantConnectionState::Stopping || m_connection == AssistantConnectionState::Probing
        || m_connection == AssistantConnectionState::Failed) { emit stateChanged(); return; }
    if (m_client.isInitialized()) m_connection = AssistantConnectionState::Ready;
    emit stateChanged();
}
bool AssistantController::busy() const
{
    return m_operation != AssistantOperation::Idle && m_operation != AssistantOperation::Uncertain;
}
bool AssistantController::canStop() const
{
    return m_operation == AssistantOperation::CreatingThread || m_operation == AssistantOperation::Submitting || m_operation == AssistantOperation::Streaming;
}
bool AssistantController::canNewChat() const
{
    return m_allowed && m_operation != AssistantOperation::Cancelling && m_operation != AssistantOperation::Reconciling
        && m_operation != AssistantOperation::Restoring;
}
bool AssistantController::canReconnect() const
{
    return m_allowed && !busy() && !loginActive() && m_connection != AssistantConnectionState::Stopping;
}
bool AssistantController::canSend() const
{
    return m_allowed && !busy() && !loginActive() && !m_pendingConfiguration && m_operation != AssistantOperation::Uncertain
        && !m_restoreUnavailable && !m_contextBlocked && !m_limitReached
        && !m_model.isEmpty() && (!m_threadOwned ? m_client.canStartConversation() : m_client.canSend());
}
QString AssistantController::statusText() const
{
    if (!m_allowed) return tr("Assistant is paused while DevHub is locked or signed out.");
    if (m_pendingConfiguration) return tr("Saved Assistant settings will apply after the active operation finishes.");
    if (loginActive()) return tr("Codex sign-in is in progress. Send is disabled.");
    if (m_contextBlocked) return tr("Workspace changed. Start a new chat to use the selected context.");
    if (m_operation == AssistantOperation::Cancelling) return tr("Stopping…");
    if (m_operation == AssistantOperation::Uncertain) return tr("Delivery is uncertain. Reconnect and check history, or start a new chat.");
    if (m_operation == AssistantOperation::CreatingThread) return tr("Creating conversation…");
    if (m_operation == AssistantOperation::Restoring) return tr("Restoring the last DevHub conversation…");
    if (m_operation == AssistantOperation::Submitting) return tr("Sending…");
    if (m_approvals && !m_approvals->pending().isEmpty()) return tr("Waiting for your approval.");
    if (m_operation == AssistantOperation::Streaming) return tr("Codex is responding…");
    if (m_operation == AssistantOperation::Reconciling) return tr("Checking accepted history…");
    if (m_connection == AssistantConnectionState::Probing) return tr("Checking the installed native Codex executable…");
    if (m_connection == AssistantConnectionState::Starting) return tr("Starting Codex and verifying its account and profile…");
    if (m_connection == AssistantConnectionState::Stopping) return tr("Closing the previous Codex process…");
    if (m_connection == AssistantConnectionState::Failed) return m_error.userMessage.isEmpty()
        ? tr("Codex startup failed. Reconnect to try again.") : m_error.userMessage;
    if (!m_client.isInitialized()) return tr("Codex is disconnected. Reconnect to check installation and status.");
    if (m_threadOwned && m_client.threadBinding().threadId.isEmpty()) return tr("The previous conversation is unavailable after reconnect. Start a new chat; the transcript is retained.");
    const auto account = m_client.account();
    if (account.mode == CodexAccountMode::Expired) return tr("Codex sign-in has expired. Reconnect after signing in through Codex.");
    if (account.mode != CodexAccountMode::ChatGpt) return tr("This Assistant requires a Codex-managed ChatGPT sign-in.");
    if (!m_client.profileVerified()) return m_profileProblems.isEmpty()
        ? tr("Codex restrictions have not passed verification. Send is disabled.")
        : tr("Codex profile blocked: %1. Send is disabled.").arg(m_profileProblems.join(QStringLiteral("; ")));
    if (!account.requiresOpenaiAuth || account.identityFingerprint.isEmpty()) return tr("The Codex account routing has not passed verification. Reconnect after checking your ChatGPT sign-in.");
    if (m_model.isEmpty() && !m_configuration.preferredModel.isEmpty()) return tr("The selected model is unavailable. Choose another model in Assistant settings.");
    if (m_model.isEmpty()) return m_modelsRequested ? tr("No text model is available in the Codex catalog.") : tr("Checking models…");
    if (!m_client.canStartConversation() && !m_client.canSend()) return m_client.compatibilityReport().explanation;
    return tr("Codex is ready. Text chat stays inside DevHub.");
}
const AssistantMessage* AssistantController::message(const QUuid& id) const
{
    for (const auto& value : m_messages) if (value.id == id) return &value;
    return nullptr;
}
AssistantMessage* AssistantController::mutableMessage(const QUuid& id)
{
    for (auto& value : m_messages) if (value.id == id) return &value;
    return nullptr;
}
qsizetype AssistantController::transcriptBytes() const
{
    qsizetype size = 0; for (const auto& value : m_messages) size += value.source.toUtf8().size(); return size;
}
bool AssistantController::send(const QString& text)
{
    if (!canSend()) return false;
    if (text.trimmed().isEmpty()) return false;
    const auto bytes = text.toUtf8().size();
    if (bytes > m_limits.userBytes) { setError(ErrorCode::AssistantLimitExceeded, tr("The message exceeds the 64 KiB text limit.")); return false; }
    if (m_messages.size() + 2 > m_limits.messages || transcriptBytes() + bytes > m_limits.transcriptBytes) { exceededLimit(); return false; }
    m_error = {}; m_itemMessages.clear(); m_pendingUser = m_ids.create();
    AssistantMessage user; user.id = m_pendingUser; user.source = text; user.createdAtUtc = m_clock.utcNow();
    m_messages.append(user);
    if (m_messages.size() == 1) {
        QString title = text.simplified();
        QString safeTitle;
        for (const auto ch : title) if (!ch.isNull() && ch.category() != QChar::Other_Control) safeTitle.append(ch);
        m_conversation.title = safeTitle.left(80);
        m_conversationCreatedAtUtc = m_clock.utcNow();
    }
    const bool create = !m_threadOwned;
    m_operation = create ? AssistantOperation::CreatingThread : AssistantOperation::Submitting;
    emit messageAdded(m_pendingUser); emit stateChanged();
    admitPendingMessage();
    return true;
}
void AssistantController::admitPendingMessage()
{
    if (m_operation == AssistantOperation::CreatingThread) {
        const auto result = m_client.startThread(m_model);
        if (!result) { m_operation = AssistantOperation::Idle; setError(result.error().code, result.error().userMessage); }
    } else submit();
}
void AssistantController::receivedThread(const CodexThread& thread)
{
    if (m_shuttingDown || !m_allowed) return;
    if (m_operation == AssistantOperation::CreatingThread) {
        m_binding = thread.binding; m_threadOwned = true; m_operation = AssistantOperation::Submitting;
        persistCurrent(); submit();
    } else if (m_operation == AssistantOperation::Restoring) {
        if (!m_restoreMetadata) {
            m_operation = AssistantOperation::Idle;
            m_restoreUnavailable = true;
            setError(ErrorCode::AssistantHistoryUnavailable, tr("The saved conversation is unavailable. Start a new chat."));
            emit stateChanged(); return;
        }
        const auto metadata = *m_restoreMetadata;
        if (thread.binding.threadId != metadata.binding.threadId
            || thread.binding.accountFingerprint != metadata.binding.accountFingerprint) {
            m_operation = AssistantOperation::Idle;
            m_restoreUnavailable = true;
            setError(ErrorCode::AssistantHistoryUnavailable, tr("The saved conversation did not match its DevHub association. Start a new chat."));
            emit stateChanged(); return;
        }
        if (metadata.lastTurnStatus == QStringLiteral("uncertain")) {
            bool found = false;
            for (const auto& turn : thread.turns) found |= !metadata.lastTurnId.isEmpty() && turn.id == metadata.lastTurnId;
            if (!found) {
                m_operation = AssistantOperation::Uncertain; m_restoreMetadata.reset();
                setError(ErrorCode::AssistantHistoryUnavailable, tr("The uncertain turn is absent from accepted history. The transcript is retained; start a new chat."));
                return;
            }
        }
        hydrateConversation(metadata, thread);
        m_restoreMetadata.reset();
    }
    else if (m_operation == AssistantOperation::Reconciling) {
        const auto* user = message(m_retryMessage); bool terminalFound = false;
        if (user && user->threadId == thread.binding.threadId) for (const auto& turn : thread.turns)
            if (turn.id == user->turnId && turn.status != CodexTurnStatus::InProgress && turn.status != CodexTurnStatus::Uncertain) terminalFound = true;
        if (terminalFound) m_reconciled.insert(m_retryMessage);
        m_operation = AssistantOperation::Idle;
        setError(ErrorCode::OperationCancelled, terminalFound ? tr("History checked. Send again starts a new turn.") : tr("The accepted turn could not be reconciled. Start a new chat."));
        QTimer::singleShot(0, this, &AssistantController::applyPendingConfiguration);
    }
}
void AssistantController::submit()
{
    const auto* user = message(m_pendingUser); if (!user) return;
    const QString source = user->source;
    const auto result = m_client.startTurn(source);
    if (!result) {
        if (auto* current = mutableMessage(m_pendingUser)) { current->delivery = AssistantDelivery::NotSent; current->final = true; emit messageChanged(current->id); }
        m_operation = AssistantOperation::Idle; setError(result.error().code, result.error().userMessage);
    } else {
        if (auto* current = mutableMessage(m_pendingUser); current && current->delivery == AssistantDelivery::NotSent) {
            current->delivery = AssistantDelivery::Queued; emit messageChanged(current->id);
        }
        emit sendAdmitted(source);
    }
}
void AssistantController::receivedItem(const CodexItem& item)
{
    if (m_shuttingDown || !m_allowed || (m_operation != AssistantOperation::Streaming && m_operation != AssistantOperation::Cancelling) || item.type != "agentMessage") return;
    const auto* user = message(m_pendingUser);
    if (!user || item.threadId != user->threadId || item.turnId != user->turnId) return;
    auto* value = mutableMessage(m_itemMessages.value(item.id));
    if (!value) {
        if (m_messages.size() >= m_limits.messages) { exceededLimit(); return; }
        AssistantMessage added; added.id = m_ids.create(); added.role = AssistantRole::Assistant; added.createdAtUtc = m_clock.utcNow();
        added.threadId = item.threadId; added.turnId = item.turnId; added.itemId = item.id; added.delivery = AssistantDelivery::Accepted;
        m_itemMessages.insert(item.id, added.id); m_messages.append(added); value = &m_messages.last(); emit messageAdded(value->id);
    }
    const auto available = qMax(qsizetype(0), m_limits.transcriptBytes - transcriptBytes() + value->source.toUtf8().size());
    const auto cap = (std::min)(m_limits.assistantBytes, available);
    const auto bytes = item.text.toUtf8();
    if (bytes.size() > cap) {
        QByteArray bounded = bytes.left(cap);
        // Never produce a partial UTF-8 scalar in the displayed source.
        while (!bounded.isEmpty() && QString::fromUtf8(bounded).toUtf8() != bounded) bounded.chop(1);
        value->source = QString::fromUtf8(bounded); value->truncated = true;
        m_changed.insert(value->id); flushMessages(); exceededLimit(); return;
    }
    value->source = item.text; value->final = item.completed;
    m_changed.insert(value->id);
    if (item.completed) flushMessages(); else if (!m_renderTimer.isActive()) m_renderTimer.start();
}
void AssistantController::flushMessages()
{
    m_renderTimer.stop(); const auto changed = std::exchange(m_changed, {});
    for (const auto& id : changed) emit messageChanged(id);
}
void AssistantController::finishedTurn(const CodexTurn& turn)
{
    if (!m_allowed && !m_shuttingDown) { m_client.stop(); return; }
    const auto* user = message(m_pendingUser);
    if (!user || user->turnId != turn.id || user->threadId != turn.threadId) return;
    m_cancelTimer.stop();
    for (const auto& item : turn.items) receivedItem(item);
    const auto delivery = turn.status == CodexTurnStatus::Completed ? AssistantDelivery::Completed
        : turn.status == CodexTurnStatus::Interrupted ? AssistantDelivery::Interrupted
        : turn.status == CodexTurnStatus::Uncertain ? AssistantDelivery::Uncertain : AssistantDelivery::Failed;
    for (auto& value : m_messages) if (value.turnId == turn.id && value.threadId == turn.threadId) {
        value.delivery = delivery; value.final = true; m_changed.insert(value.id);
    }
    m_operation = turn.status == CodexTurnStatus::Uncertain ? AssistantOperation::Uncertain : AssistantOperation::Idle;
    if (turn.hasError) m_error = turn.error;
    const QString turnStatus = turn.status == CodexTurnStatus::Completed ? QStringLiteral("completed")
        : turn.status == CodexTurnStatus::Interrupted ? QStringLiteral("interrupted")
        : turn.status == CodexTurnStatus::Uncertain ? QStringLiteral("uncertain") : QStringLiteral("failed");
    persistCurrent(turnStatus);
    flushMessages(); emit stateChanged();
    if (std::exchange(m_newChatWanted, false)) resetConversation();
    if (m_shuttingDown || !m_allowed) { m_client.stop(); return; }
    QTimer::singleShot(0, this, &AssistantController::applyPendingConfiguration);
}
void AssistantController::stopTurn()
{
    if (!canStop()) return;
    m_operation = AssistantOperation::Cancelling; emit stateChanged(); m_cancelTimer.start();
    if (m_client.activeTurnId().isEmpty()) m_client.stop();
    else {
        const auto result = m_client.interruptTurn();
        if (!result) { setError(result.error().code, result.error().userMessage); m_client.stop(); }
    }
}
void AssistantController::newChat()
{
    if (!canNewChat()) return;
    // Explicitly choosing New Chat must survive reconnect/profile relaunch;
    // otherwise the same failing saved association is selected again.
    m_autoRestoreSuppressed = true;
    if (busy()) { m_newChatWanted = true; stopTurn(); return; }
    if (m_backendOwned && m_operation == AssistantOperation::Uncertain) {
        // A new native thread must be selected lazily, without accidentally
        // continuing the old one. Reconnect retains Codex's durable history.
        m_newChatWanted = true; reconnect();
    } else {
        const bool reconnectNeeded = m_connection == AssistantConnectionState::Failed
            || m_connection == AssistantConnectionState::Disconnected;
        resetConversation();
        if (reconnectNeeded) reconnect();
    }
}
void AssistantController::resetConversation(bool retainAssociation)
{
    setTaskDataDisclosureGrant(false);
    if (retainAssociation && m_threadOwned && !m_binding.threadId.isEmpty() && !m_messages.isEmpty()) {
        AssistantConversationMetadata previous;
        previous.conversation = m_conversation; previous.binding = m_binding;
        previous.createdAtUtc = m_conversationCreatedAtUtc.isValid() ? m_conversationCreatedAtUtc : m_messages.first().createdAtUtc;
        previous.updatedAtUtc = m_clock.utcNow(); previous.lastOpenedAtUtc = previous.updatedAtUtc;
        previous.devhubAccountId = m_accountId; previous.lastTurnId = m_messages.last().turnId;
        previous.lastTurnStatus = QStringLiteral("closed");
        if (m_sessionConversations.size() >= 100) m_sessionConversations.removeFirst();
        m_sessionConversations.append(previous);
        if (m_restoreEnabled && m_repository && !m_accountId.isEmpty()) (void)m_repository->save(previous);
    }
    m_renderTimer.stop(); m_changed.clear(); m_reconciled.clear(); m_messages.clear(); m_itemMessages.clear();
    m_pendingUser = QUuid{}; m_retryMessage = QUuid{}; m_operation = AssistantOperation::Idle; m_contextBlocked = false; m_limitReached = false; m_error = {};
    m_threadOwned = false;
    m_restoreUnavailable = false;
    m_restoreMetadata.reset();
    m_binding = {};
    m_conversation = {}; m_conversation.id = m_ids.create(); m_conversation.context = m_selectedContext;
    m_conversationCreatedAtUtc = {};
    emit transcriptReset(); emit stateChanged();
}
bool AssistantController::canRetry(const QUuid& id) const
{
    const auto* value = message(id);
    if (m_reconciled.contains(id) && !canSend()) return false;
    return value && value->role == AssistantRole::User && m_allowed && !busy() && m_operation != AssistantOperation::Uncertain && !m_contextBlocked && !m_limitReached
        && ((value->delivery == AssistantDelivery::NotSent && canSend()) || ((value->delivery == AssistantDelivery::Failed || value->delivery == AssistantDelivery::Interrupted)
        && !value->turnId.isEmpty() && m_client.isInitialized() && m_client.profileVerified()));
}
bool AssistantController::retryIsNewTurn(const QUuid& id) const { return m_reconciled.contains(id); }
void AssistantController::retry(const QUuid& id)
{
    if (!canRetry(id)) return;
    const auto* value = message(id); const auto source = value->source;
    if (value->delivery == AssistantDelivery::NotSent) {
        m_pendingUser = id; m_itemMessages.clear(); m_error = {};
        mutableMessage(id)->final = false;
        m_operation = !m_threadOwned ? AssistantOperation::CreatingThread : AssistantOperation::Submitting;
        emit messageChanged(id); emit stateChanged(); admitPendingMessage(); return;
    }
    if (m_reconciled.contains(id)) { (void)send(source); return; }
    m_retryMessage = id; m_operation = AssistantOperation::Reconciling; emit stateChanged();
    const auto result = m_client.readThread(m_binding);
    if (!result) { m_operation = AssistantOperation::Idle; setError(result.error().code, result.error().userMessage); }
}
void AssistantController::setContext(AssistantContext context)
{
    if (context == m_selectedContext) return;
    if (m_notesDisclosureGranted || m_filesDisclosureGranted) setToolDisclosureGrants(false, false);
    if (m_taskDataDisclosureGranted) setTaskDataDisclosureGrant(false);
    if (!m_messages.isEmpty() && !sameContextBinding(context, m_conversation.context)) revokeAccess();
    m_selectedContext = std::move(context);
    if (m_messages.isEmpty()) m_conversation.context = m_selectedContext;
    else if (!sameContextBinding(m_selectedContext, m_conversation.context)) { m_contextBlocked = true; stopTurn(); }
    emit stateChanged();
}
void AssistantController::setPersistenceContext(QString accountId, bool restoreEnabled)
{
    if (!m_accountId.isEmpty() && m_accountId != accountId) revokeAccess();
    if (m_accountId != accountId) { m_restoreAttempted = false; m_autoRestoreSuppressed = false; }
    m_accountId = std::move(accountId); m_restoreEnabled = restoreEnabled;
    maybeRestoreConversation();
}
void AssistantController::setSessionAllowed(bool allowed, bool clearTranscript)
{
    if (m_shuttingDown && allowed) return;
    const bool wasAllowed = m_allowed;
    m_allowed = allowed;
    if (!allowed) {
        m_startupTimer.stop();
        if (wasAllowed) revokeAccess();
        m_loginPending = false; m_challenge.reset(); m_loginStatus.clear();
        if (wasAllowed) {
            ++m_probeGeneration; emit cancelProbeRequested(); m_reconnectWanted = false;
            stopBackendAfterInterrupt();
        }
        if (clearTranscript) { m_sessionConversations.clear(); resetConversation(false); }
    } else if (m_opened) {
        if (m_backendOwned && !m_client.isInitialized()) m_reconnectWanted = true;
        else open();
    }
    emit stateChanged();
}
void AssistantController::revokeAccess()
{
    setToolDisclosureGrants(false, false);
    setTaskDataDisclosureGrant(false);
    ++m_accessGeneration;
    emit accessRevoked(m_accessGeneration);
}

void AssistantController::setToolDisclosureGrants(bool notes, bool files)
{
    if (m_notesDisclosureGranted == notes && m_filesDisclosureGranted == files) return;
    m_notesDisclosureGranted = notes;
    m_filesDisclosureGranted = files;
    emit toolDisclosureChanged(notes, files);
}

void AssistantController::setTaskDataDisclosureGrant(bool granted)
{
    if (m_taskDataDisclosureGranted == granted) return;
    m_taskDataDisclosureGranted = granted;
    emit taskDataDisclosureChanged(granted);
}

void AssistantController::reportToolActivity(QString toolName, bool active, bool success)
{
    static const QSet<QString> safeTools{QStringLiteral("git.status"), QStringLiteral("git.branches"),
        QStringLiteral("git.log"), QStringLiteral("workspace.metadata"), QStringLiteral("services.list"),
        QStringLiteral("notes.read"), QStringLiteral("notes.search"), QStringLiteral("files.read"),
        QStringLiteral("files.search"), QStringLiteral("something_tasks.find"),
        QStringLiteral("something_tasks.read")};
    if (safeTools.contains(toolName)) emit toolActivityChanged(std::move(toolName), active, success);
}
void AssistantController::stopBackendAfterInterrupt()
{
    if (!m_backendOwned) {
        if (m_shuttingDown) QTimer::singleShot(0, this, &AssistantController::completeShutdown);
        return;
    }
    if (!m_client.activeTurnId().isEmpty()) {
        m_operation = AssistantOperation::Cancelling;
        m_cancelTimer.start(m_limits.cancellationMilliseconds);
        const auto interrupted = m_client.interruptTurn();
        if (!interrupted) m_client.stop();
        emit stateChanged();
        return;
    }
    m_client.stop();
}
void AssistantController::beginShutdown(std::function<void()> completion)
{
    if (m_shutdownComplete) {
        if (completion) QTimer::singleShot(0, this, [completion = std::move(completion)] { completion(); });
        return;
    }
    if (completion) m_shutdownCompletion = std::move(completion);
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    m_startupTimer.stop();
    m_allowed = false;
    revokeAccess();
    m_loginPending = false; m_challenge.reset(); m_loginStatus.clear();
    ++m_probeGeneration; emit cancelProbeRequested();
    m_reconnectWanted = false; m_newChatWanted = false;
    m_renderTimer.stop(); m_changed.clear();
    stopBackendAfterInterrupt();
    if (!m_backendOwned) QTimer::singleShot(0, this, &AssistantController::completeShutdown);
    emit stateChanged();
}
void AssistantController::completeShutdown()
{
    if (!m_shuttingDown || m_shutdownComplete || m_backendOwned) return;
    m_shutdownComplete = true;
    if (m_shutdownCompletion) {
        auto completion = std::exchange(m_shutdownCompletion, {});
        completion();
    }
    emit shutdownFinished();
}
QStringList AssistantController::diagnosticsSnapshot() const
{
    const auto connection = [this] {
        switch (m_connection) {
        case AssistantConnectionState::Disconnected: return tr("disconnected");
        case AssistantConnectionState::Probing: return tr("checking executable");
        case AssistantConnectionState::Starting: return tr("starting");
        case AssistantConnectionState::Ready: return tr("ready");
        case AssistantConnectionState::Stopping: return tr("stopping");
        case AssistantConnectionState::Failed: return tr("failed");
        }
        return tr("unknown");
    }();
    const auto operation = [this] {
        switch (m_operation) {
        case AssistantOperation::Idle: return tr("idle");
        case AssistantOperation::CreatingThread: return tr("creating thread");
        case AssistantOperation::Restoring: return tr("restoring");
        case AssistantOperation::Submitting: return tr("submitting");
        case AssistantOperation::Streaming: return tr("streaming");
        case AssistantOperation::Cancelling: return tr("cancelling");
        case AssistantOperation::Reconciling: return tr("reconciling uncertain delivery");
        case AssistantOperation::Uncertain: return tr("delivery uncertain");
        }
        return tr("unknown");
    }();
    const QString version = m_runtimeVersion.isEmpty() ? tr("unverified") : m_runtimeVersion;
    const QString profile = m_client.profileVerified() ? tr("verified") : tr("unverified");
    return {tr("Assistant connection: %1").arg(connection),
            tr("Assistant operation: %1").arg(operation),
            tr("Codex runtime: %1; profile: %2").arg(version, profile),
            tr("Codex compatibility: %1 (%2); adapter: %3; binary: %4; configuration: %5; policy: %6")
                .arg(m_client.compatibilityReport().stateText(), m_client.compatibilityReport().reasonCode(),
                    m_client.compatibilityReport().adapter, m_client.compatibilityReport().binaryIdentity,
                    m_client.compatibilityReport().configurationIdentity, m_client.compatibilityReport().policyIdentity),
            tr("Codex profile restrictions: %1").arg(m_profileProblems.isEmpty() ? tr("none reported") : m_profileProblems.join(QStringLiteral("; "))),
            tr("Assistant access generation: %1").arg(m_accessGeneration)};
}
void AssistantController::failedRequest(const QString& method, const domain::errors::AppError& error)
{
    if (m_shuttingDown) return;
    if (method == "account/read" && error.code == ErrorCode::OperationCancelled) return;
    if (method == "thread/resume" && m_operation == AssistantOperation::Restoring) {
        m_operation = AssistantOperation::Idle; m_threadOwned = false; m_restoreMetadata.reset();
        m_restoreUnavailable = true;
        setError(ErrorCode::AssistantHistoryUnavailable, tr("The saved conversation is missing or incompatible. Start a new chat."));
        emit stateChanged(); QTimer::singleShot(0, this, &AssistantController::applyPendingConfiguration); return;
    }
    if (method == "thread/start" && m_operation == AssistantOperation::CreatingThread) {
        m_operation = error.code == ErrorCode::CodexRequestTimeout || error.code == ErrorCode::CodexConnectionLost ? AssistantOperation::Uncertain : AssistantOperation::Idle;
        if (auto* value = mutableMessage(m_pendingUser)) {
            value->delivery = m_operation == AssistantOperation::Uncertain ? AssistantDelivery::Uncertain : AssistantDelivery::NotSent;
            value->final = true; emit messageChanged(value->id);
        }
    } else if (method == "turn/start" && (m_operation == AssistantOperation::Submitting || m_operation == AssistantOperation::Cancelling)) {
        if (auto* value = mutableMessage(m_pendingUser)) { value->delivery = AssistantDelivery::Uncertain; value->final = true; emit messageChanged(value->id); }
        m_operation = AssistantOperation::Uncertain;
        persistCurrent(QStringLiteral("uncertain"));
    } else if (method == "thread/read" && m_operation == AssistantOperation::Reconciling) m_operation = AssistantOperation::Idle;
    if (method == "account/login/start" || method == "account/login/cancel") { m_loginPending = false; m_challenge.reset(); m_loginStatus = tr("Sign-in could not be completed. Recheck the connection and try again."); }
    setError(error.code, error.userMessage);
    QTimer::singleShot(0, this, &AssistantController::applyPendingConfiguration);
}
void AssistantController::selectModel()
{
    m_model.clear();
    for (const auto& model : m_models) {
        if (model.hidden || !model.supportsText || !model.availabilityMessage.isEmpty()) continue;
        if (!m_configuration.preferredModel.isEmpty()) {
            if (model.model == m_configuration.preferredModel) { m_model = model.model; break; }
        } else if (m_model.isEmpty() || model.isDefault) m_model = model.model;
    }
}
void AssistantController::applyConfiguration(AssistantConfiguration configuration)
{
    if (configuration == m_configuration && !m_pendingConfiguration) return;
    m_pendingConfiguration = std::move(configuration); emit stateChanged(); applyPendingConfiguration();
}
void AssistantController::applyPendingConfiguration()
{
    if (!m_pendingConfiguration || busy() || loginActive() || m_connection == AssistantConnectionState::Stopping) return;
    m_configuration = std::move(*m_pendingConfiguration); m_pendingConfiguration.reset(); selectModel();
    if (m_opened && m_allowed) reconnect(); else emit stateChanged();
}
bool AssistantController::canConnectAccount() const
{
    return m_allowed && !busy() && !loginActive() && !m_pendingConfiguration
        && m_client.isInitialized() && m_client.profileVerified() && m_connection == AssistantConnectionState::Ready;
}
void AssistantController::connectAccount(CodexLoginMode mode)
{
    if (!canConnectAccount()) return;
    m_loginPending = true; m_challenge.reset(); m_loginStatus = tr("Requesting a Codex sign-in challenge…"); emit stateChanged();
    const auto result = m_client.startLogin(mode);
    if (!result) { m_loginPending = false; m_loginStatus = tr("Sign-in could not be started."); setError(result.error().code, result.error().userMessage); }
}
void AssistantController::cancelAccountLogin()
{
    if (!loginActive()) return;
    if (!m_challenge) { m_loginPending = false; m_loginStatus = tr("Sign-in cancelled before a challenge was received."); m_client.stop(); emit stateChanged(); return; }
    const auto result = m_client.cancelLogin();
    m_loginPending = false; m_challenge.reset();
    if (!result) setError(result.error().code, result.error().userMessage);
    emit stateChanged(); QTimer::singleShot(0, this, &AssistantController::applyPendingConfiguration);
}
QString AssistantController::runtimeText() const
{
    const auto mode = m_client.account().mode;
    const QString account = mode == CodexAccountMode::SignedOut ? tr("Signed out") : mode == CodexAccountMode::ChatGpt ? tr("ChatGPT")
        : mode == CodexAccountMode::ApiKey ? tr("API key account — unsupported") : mode == CodexAccountMode::Expired ? tr("Expired sign-in")
        : mode == CodexAccountMode::Other ? tr("Other account — unsupported") : tr("Not checked");
    const QString process = m_connection == AssistantConnectionState::Probing ? tr("Checking executable")
        : m_connection == AssistantConnectionState::Starting ? tr("Starting") : m_connection == AssistantConnectionState::Stopping ? tr("Stopping")
        : m_connection == AssistantConnectionState::Failed ? tr("Failed") : m_client.isInitialized() ? tr("Running") : tr("Disconnected");
    const auto report = m_client.compatibilityReport();
    const auto restrictionStatus = !m_client.profileVerified() ? tr("Not verified")
        : report.runtimePolicyVerified ? tr("Verified") : tr("Configuration checked; behavior verification required");
    const auto explanation = report.reason == CodexCompatibilityReason::RuntimeRestrictionFailed && !m_profileProblems.isEmpty()
        ? tr("Runtime restriction check failed: %1.").arg(m_profileProblems.join(QStringLiteral("; ")))
        : report.explanation;
    return tr("Version: %1\nProcess: %2\nAccount: %3\nRestrictions: %4\nCompatibility: %5\n%6")
        .arg(m_runtimeVersion.isEmpty() ? tr("Not checked") : m_runtimeVersion, process, account,
            restrictionStatus, report.stateText(), explanation);
}
bool AssistantController::canVerifyCompatibility() const
{
    return m_allowed && !busy() && !loginActive() && !m_pendingConfiguration
        && m_client.isInitialized() && m_client.profileVerified() && !compatibilityVerificationActive()
        && m_client.compatibilityReport().protocolCompatible;
}
void AssistantController::verifyCompatibility()
{
    if (canVerifyCompatibility()) (void)m_client.verifyCompatibility();
}
void AssistantController::setError(ErrorCode code, const QString& text)
{
    m_error = domain::errors::AppError::failure(code, domain::errors::ErrorCategory::Process, text, {}, QStringLiteral("assistant.controller"));
    emit stateChanged();
}
void AssistantController::exceededLimit()
{
    m_limitReached = true; setError(ErrorCode::AssistantLimitExceeded, tr("The conversation limit was reached. Partial text is retained; start a new chat.")); stopTurn();
}
shared::common::Result<QList<AssistantConversationMetadata>> AssistantController::savedConversations(const QString& accountId) const
{
    if (m_repository) return m_repository->list(accountId, 100);
    return shared::common::Result<QList<AssistantConversationMetadata>>::failure(domain::errors::AppError::failure(
        ErrorCode::AssistantHistoryUnavailable, domain::errors::ErrorCategory::Persistence,
        tr("Saved Assistant conversations are not available in this session-only version."), {}, QStringLiteral("assistant.history")));
}

void AssistantController::persistCurrent(const QString& turnStatus)
{
    if (!m_restoreEnabled || !m_repository || m_accountId.isEmpty() || !m_threadOwned || m_binding.threadId.isEmpty()) return;
    AssistantConversationMetadata metadata;
    metadata.conversation = m_conversation; metadata.devhubAccountId = m_accountId; metadata.binding = m_binding;
    if (!m_conversationCreatedAtUtc.isValid()) m_conversationCreatedAtUtc = m_clock.utcNow();
    metadata.createdAtUtc = m_conversationCreatedAtUtc;
    metadata.updatedAtUtc = m_clock.utcNow(); metadata.lastOpenedAtUtc = metadata.updatedAtUtc;
    metadata.lastTurnId = m_messages.isEmpty() ? QString{} : m_messages.last().turnId;
    metadata.lastTurnStatus = turnStatus;
    const auto result = m_repository->save(metadata);
    if (!result) setError(result.error().code, tr("The live conversation is continuing, but its local resume information could not be saved."));
}

void AssistantController::maybeRestoreConversation()
{
    if (m_restoreAttempted || !m_allowed
        || !m_client.isInitialized() || !m_client.profileVerified() || m_client.account().mode != CodexAccountMode::ChatGpt
        || !m_client.account().requiresOpenaiAuth || m_client.account().identityFingerprint.isEmpty()
        || (!m_client.canStartConversation() && !m_client.canSend())
        || m_model.isEmpty() || busy()) return;
    const bool current = !m_binding.threadId.isEmpty() && m_client.threadBinding().threadId.isEmpty();
    if (!current && (m_autoRestoreSuppressed || !m_restoreEnabled || !m_repository || m_accountId.isEmpty()
        || !m_messages.isEmpty() || m_operation != AssistantOperation::Idle)) return;
    m_restoreAttempted = true;
    AssistantConversationMetadata metadata;
    if (current) {
        metadata.conversation = m_conversation; metadata.devhubAccountId = m_accountId; metadata.binding = m_binding;
        metadata.createdAtUtc = m_conversationCreatedAtUtc;
        metadata.lastTurnId = m_messages.isEmpty() ? QString{} : m_messages.last().turnId;
        if (m_operation == AssistantOperation::Uncertain) metadata.lastTurnStatus = QStringLiteral("uncertain");
    } else {
        const auto saved = m_repository->list(m_accountId, 100);
        if (!saved) { setError(saved.error().code, tr("The live chat is available, but saved conversation metadata could not be read.")); return; }
        if (saved.value().isEmpty()) return;
        metadata = saved.value().first();
    }
    if (!sameContextBinding(metadata.conversation.context, m_selectedContext)) {
        m_restoreUnavailable = true;
        setError(ErrorCode::AssistantHistoryUnavailable, tr("The saved conversation belongs to a different workspace. Start a new chat.")); return;
    }
    if (metadata.devhubAccountId != m_accountId || metadata.binding.accountFingerprint != m_client.account().identityFingerprint
        || metadata.binding.threadId.isEmpty()) {
        m_restoreUnavailable = true;
        setError(ErrorCode::AssistantHistoryUnavailable, tr("The saved conversation belongs to a different Codex account. Start a new chat.")); return;
    }
    m_restoreMetadata = metadata;
    m_conversation = metadata.conversation; m_conversationCreatedAtUtc = metadata.createdAtUtc;
    m_binding = metadata.binding; m_binding.scratchDirectory = m_scratch;
    if (!current) m_binding.codexHome.clear();
    m_threadOwned = true; m_operation = AssistantOperation::Restoring;
    const auto result = m_client.resumeThread(m_binding);
    if (!result) {
        m_restoreMetadata.reset(); m_threadOwned = false; m_operation = AssistantOperation::Idle;
        m_restoreUnavailable = true;
        setError(ErrorCode::AssistantHistoryUnavailable, tr("The saved conversation is incompatible or no longer available. Start a new chat."));
    }
    emit stateChanged();
}

void AssistantController::hydrateConversation(const AssistantConversationMetadata& metadata, const CodexThread& thread)
{
    m_renderTimer.stop(); m_changed.clear(); m_reconciled.clear(); m_pendingUser = QUuid{}; m_retryMessage = QUuid{};
    m_messages.clear(); m_itemMessages.clear(); m_binding = thread.binding; m_threadOwned = true;
    m_error = {}; m_limitReached = false;
    m_conversation = metadata.conversation; m_conversationCreatedAtUtc = metadata.createdAtUtc;
    emit transcriptReset();
    QList<AssistantMessage> hydrated;
    for (const auto& turn : thread.turns) for (const auto& item : turn.items) {
        if (item.type != QStringLiteral("userMessage") && item.type != QStringLiteral("agentMessage")) continue;
        if (item.text.isEmpty()) continue;
        AssistantMessage message; message.id = m_ids.create();
        message.role = item.type == QStringLiteral("userMessage") ? AssistantRole::User : AssistantRole::Assistant;
        message.source = item.text; message.threadId = item.threadId; message.turnId = item.turnId; message.itemId = item.id;
        message.createdAtUtc = m_clock.utcNow(); message.final = true;
        message.delivery = turn.status == CodexTurnStatus::Completed ? AssistantDelivery::Completed
            : turn.status == CodexTurnStatus::Interrupted ? AssistantDelivery::Interrupted : AssistantDelivery::Failed;
        if (metadata.lastTurnStatus == QStringLiteral("uncertain") && turn.id == metadata.lastTurnId
            && message.role == AssistantRole::User && turn.status != CodexTurnStatus::Completed)
            m_reconciled.insert(message.id);
        hydrated.append(std::move(message));
    }
    while (hydrated.size() > m_limits.messages) hydrated.removeFirst();
    qsizetype bytes = 0;
    for (const auto& message : hydrated) bytes += message.source.toUtf8().size();
    while (!hydrated.isEmpty() && bytes > m_limits.transcriptBytes) {
        bytes -= hydrated.first().source.toUtf8().size(); hydrated.removeFirst();
    }
    for (auto& message : hydrated) {
        if (message.role == AssistantRole::Assistant && message.source.toUtf8().size() > m_limits.assistantBytes) {
            QByteArray bounded = message.source.toUtf8().left(m_limits.assistantBytes);
            while (!bounded.isEmpty() && QString::fromUtf8(bounded).toUtf8() != bounded) bounded.chop(1);
            message.source = QString::fromUtf8(bounded); message.truncated = true;
        }
        m_messages.append(message); emit messageAdded(message.id);
    }
    m_operation = AssistantOperation::Idle;
    m_restoreUnavailable = false;
    m_contextBlocked = !sameContextBinding(m_selectedContext, m_conversation.context);
    if (!m_contextBlocked) m_conversation.context.name = m_selectedContext.name;
    const auto lastStatus = thread.turns.isEmpty() ? QString{} : thread.turns.last().status == CodexTurnStatus::Completed
        ? QStringLiteral("completed") : thread.turns.last().status == CodexTurnStatus::Interrupted
        ? QStringLiteral("interrupted") : QStringLiteral("failed");
    persistCurrent(lastStatus); emit stateChanged();
}
}
