#include "AssistantController.h"
#include <algorithm>
#include <utility>

namespace devhub::application::assistant {
using domain::errors::ErrorCode;
AssistantController::AssistantController(ICodexClient& client, shared::time::IApplicationClock& clock,
    shared::identifiers::IIdGenerator& ids, QString scratchDirectory, QStringList launchArguments,
    QProcessEnvironment environment, IConversationRepository* repository, QObject* parent, AssistantLimits limits)
    : QObject(parent), m_client(client), m_clock(clock), m_ids(ids), m_repository(repository),
      m_scratch(std::move(scratchDirectory)), m_arguments(std::move(launchArguments)),
      m_environment(std::move(environment)), m_limits(limits)
{
    resetConversation();
    m_renderTimer.setSingleShot(true); m_renderTimer.setInterval(m_limits.renderMilliseconds);
    m_cancelTimer.setSingleShot(true); m_cancelTimer.setInterval(m_limits.cancellationMilliseconds);
    connect(&m_renderTimer, &QTimer::timeout, this, &AssistantController::flushMessages);
    connect(&m_cancelTimer, &QTimer::timeout, this, [this] { m_client.stop(); });
    connect(&client, &ICodexClient::initialized, this, [this] {
        m_modelsRequested = true;
        const auto result = m_client.listModels();
        if (!result) setError(result.error().code, result.error().userMessage);
        refreshConnection();
    });
    connect(&client, &ICodexClient::modelsReceived, this, [this](const CodexModelPage& page) {
        for (const auto& model : page.models) if (!model.hidden && model.supportsText && (m_model.isEmpty() || model.isDefault)) m_model = model.model;
        refreshConnection();
    });
    connect(&client, &ICodexClient::accountChanged, this, [this] { refreshConnection(); });
    connect(&client, &ICodexClient::profileChecked, this, [this](bool accepted) {
        if (!accepted) setError(ErrorCode::AssistantPermissionDenied, tr("The Codex profile does not meet this Assistant's restrictions."));
        refreshConnection();
    });
    connect(&client, &ICodexClient::threadReceived, this, &AssistantController::receivedThread);
    connect(&client, &ICodexClient::turnAccepted, this, [this](const QString& threadId, const QString& turnId) {
        if (m_operation != AssistantOperation::Submitting && m_operation != AssistantOperation::Cancelling) return;
        if (auto* user = mutableMessage(m_pendingUser)) {
            user->delivery = AssistantDelivery::Accepted; user->threadId = threadId; user->turnId = turnId; emit messageChanged(user->id);
        }
        if (m_operation == AssistantOperation::Cancelling) {
            const auto interrupted = m_client.interruptTurn(); Q_UNUSED(interrupted);
        } else m_operation = AssistantOperation::Streaming;
        emit stateChanged();
    });
    connect(&client, &ICodexClient::itemChanged, this, &AssistantController::receivedItem);
    connect(&client, &ICodexClient::turnFinished, this, &AssistantController::finishedTurn);
    connect(&client, &ICodexClient::requestFailed, this, [this](qint64, const QString& method, const domain::errors::AppError& error) { failedRequest(method, error); });
    connect(&client, &ICodexClient::protocolFailed, this, [this](const domain::errors::AppError& error) { setError(error.code, error.userMessage); });
    connect(&client, &ICodexClient::connectionClosed, this, [this](quint64 generation) {
        if (generation != m_processGeneration) return;
        m_connection = m_reconnectWanted ? AssistantConnectionState::Stopping : AssistantConnectionState::Disconnected;
        if (busy()) {
            if (auto* user = mutableMessage(m_pendingUser)) { user->delivery = AssistantDelivery::Uncertain; user->final = true; emit messageChanged(user->id); }
            m_operation = AssistantOperation::Uncertain;
        }
        emit stateChanged();
    });
    connect(&client, &ICodexClient::backendStopped, this, [this](quint64 generation) {
        if (generation != m_processGeneration) return;
        m_backendOwned = false; m_cancelTimer.stop();
        if (m_newChatWanted) { m_newChatWanted = false; resetConversation(); }
        if (std::exchange(m_reconnectWanted, false) && m_allowed) beginProbe();
        else { m_connection = AssistantConnectionState::Disconnected; emit stateChanged(); }
    });
}
void AssistantController::open()
{
    m_opened = true;
    if (m_allowed && m_connection == AssistantConnectionState::Disconnected && !m_backendOwned) beginProbe();
}
void AssistantController::beginProbe()
{
    m_connection = AssistantConnectionState::Probing; m_modelsRequested = false; m_model.clear(); m_error = {};
    emit stateChanged(); emit executableProbeRequested({}, ++m_probeGeneration);
}
void AssistantController::reconnect()
{
    if (!canReconnect()) return;
    emit cancelProbeRequested(); ++m_probeGeneration;
    if (m_backendOwned) {
        m_reconnectWanted = true; m_connection = AssistantConnectionState::Stopping; emit stateChanged(); m_client.stop();
    } else beginProbe();
}
void AssistantController::executableFound(CodexExecutable executable, quint64 generation)
{
    if (!m_allowed || generation != m_probeGeneration || m_connection != AssistantConnectionState::Probing) return;
    m_backendOwned = true; m_connection = AssistantConnectionState::Starting; emit stateChanged();
    const auto result = m_client.launch({std::move(executable), m_arguments, m_scratch, m_environment, ++m_processGeneration});
    if (!result) { m_backendOwned = false; m_connection = AssistantConnectionState::Failed; setError(result.error().code, result.error().userMessage); }
}
void AssistantController::probeFailed(domain::errors::AppError error, quint64 generation)
{
    if (generation != m_probeGeneration || !m_allowed) return;
    m_connection = AssistantConnectionState::Failed; setError(error.code, error.userMessage);
}
void AssistantController::refreshConnection()
{
    if (!m_allowed || m_connection == AssistantConnectionState::Stopping || m_connection == AssistantConnectionState::Probing) { emit stateChanged(); return; }
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
    return m_allowed && m_operation != AssistantOperation::Cancelling && m_operation != AssistantOperation::Reconciling;
}
bool AssistantController::canReconnect() const
{
    return m_allowed && !busy() && m_connection != AssistantConnectionState::Stopping;
}
bool AssistantController::canSend() const
{
    return m_allowed && !busy() && m_operation != AssistantOperation::Uncertain && !m_contextBlocked && !m_limitReached
        && !m_model.isEmpty() && (!m_threadOwned ? m_client.canStartConversation() : m_client.canSend());
}
QString AssistantController::statusText() const
{
    if (!m_allowed) return tr("Assistant is paused while DevHub is locked or signed out.");
    if (m_contextBlocked) return tr("Workspace changed. Start a new chat to use the selected context.");
    if (m_operation == AssistantOperation::Cancelling) return tr("Stopping…");
    if (m_operation == AssistantOperation::Uncertain) return tr("Delivery is uncertain. Reconnect and check history, or start a new chat.");
    if (m_operation == AssistantOperation::CreatingThread) return tr("Creating conversation…");
    if (m_operation == AssistantOperation::Submitting) return tr("Sending…");
    if (m_operation == AssistantOperation::Streaming) return tr("Codex is responding…");
    if (m_operation == AssistantOperation::Reconciling) return tr("Checking accepted history…");
    if (m_connection == AssistantConnectionState::Probing) return tr("Checking the installed native Codex executable…");
    if (m_connection == AssistantConnectionState::Starting) return tr("Starting Codex and verifying its account and profile…");
    if (m_connection == AssistantConnectionState::Stopping) return tr("Closing the previous Codex process…");
    if (!m_client.isInitialized()) return tr("Codex is disconnected. Reconnect to check installation and status.");
    if (m_threadOwned && m_client.threadBinding().threadId.isEmpty()) return tr("The previous conversation is unavailable after reconnect. Start a new chat; the transcript is retained.");
    const auto account = m_client.account();
    if (account.mode == CodexAccountMode::Expired) return tr("Codex sign-in has expired. Reconnect after signing in through Codex.");
    if (account.mode != CodexAccountMode::ChatGpt) return tr("This Assistant requires a Codex-managed ChatGPT sign-in.");
    if (!m_client.profileVerified()) return tr("Codex restrictions have not passed verification. Send is disabled.");
    if (!m_client.canStartConversation() && !m_client.canSend()) return tr("Native chat awaits authenticated model and runtime verification. Send is disabled.");
    if (m_model.isEmpty()) return m_modelsRequested ? tr("No text model is available in the Codex catalog.") : tr("Checking models…");
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
    if (m_operation == AssistantOperation::CreatingThread) { m_binding = thread.binding; m_threadOwned = true; m_operation = AssistantOperation::Submitting; submit(); }
    else if (m_operation == AssistantOperation::Reconciling) {
        const auto* user = message(m_retryMessage); bool terminalFound = false;
        if (user && user->threadId == thread.binding.threadId) for (const auto& turn : thread.turns)
            if (turn.id == user->turnId && turn.status != CodexTurnStatus::InProgress && turn.status != CodexTurnStatus::Uncertain) terminalFound = true;
        if (terminalFound) m_reconciled.insert(m_retryMessage);
        m_operation = AssistantOperation::Idle;
        setError(ErrorCode::OperationCancelled, terminalFound ? tr("History checked. Send again starts a new turn.") : tr("The accepted turn could not be reconciled. Start a new chat."));
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
    if (!m_allowed || (m_operation != AssistantOperation::Streaming && m_operation != AssistantOperation::Cancelling) || item.type != "agentMessage") return;
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
    flushMessages(); emit stateChanged();
    if (std::exchange(m_newChatWanted, false)) resetConversation();
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
    if (busy()) { m_newChatWanted = true; stopTurn(); return; }
    if (m_backendOwned && m_operation == AssistantOperation::Uncertain) {
        // A new native thread must be selected lazily, without accidentally
        // continuing the old one. Reconnect retains Codex's durable history.
        m_newChatWanted = true; reconnect();
    } else resetConversation();
}
void AssistantController::resetConversation(bool retainAssociation)
{
    if (retainAssociation && m_threadOwned && !m_binding.threadId.isEmpty() && !m_messages.isEmpty()) {
        AssistantConversationMetadata previous;
        previous.conversation = m_conversation; previous.binding = m_binding;
        previous.createdAtUtc = m_messages.first().createdAtUtc; previous.updatedAtUtc = m_clock.utcNow();
        if (m_sessionConversations.size() >= 100) m_sessionConversations.removeFirst();
        m_sessionConversations.append(previous);
    }
    m_renderTimer.stop(); m_changed.clear(); m_reconciled.clear(); m_messages.clear(); m_itemMessages.clear();
    m_pendingUser = QUuid{}; m_retryMessage = QUuid{}; m_operation = AssistantOperation::Idle; m_contextBlocked = false; m_limitReached = false; m_error = {};
    m_threadOwned = false;
    m_binding = {};
    m_conversation = {}; m_conversation.id = m_ids.create(); m_conversation.context = m_selectedContext;
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
    m_selectedContext = std::move(context);
    if (m_messages.isEmpty()) m_conversation.context = m_selectedContext;
    else if (!(m_selectedContext == m_conversation.context)) { m_contextBlocked = true; stopTurn(); }
    emit stateChanged();
}
void AssistantController::setSessionAllowed(bool allowed, bool clearTranscript)
{
    m_allowed = allowed;
    if (!allowed) {
        ++m_probeGeneration; emit cancelProbeRequested(); m_reconnectWanted = false; m_client.stop();
        if (clearTranscript) { m_sessionConversations.clear(); resetConversation(false); }
    } else if (m_opened) {
        if (m_backendOwned && !m_client.isInitialized()) m_reconnectWanted = true;
        else open();
    }
    emit stateChanged();
}
void AssistantController::failedRequest(const QString& method, const domain::errors::AppError& error)
{
    if (method == "thread/start" && m_operation == AssistantOperation::CreatingThread) {
        m_operation = error.code == ErrorCode::CodexRequestTimeout || error.code == ErrorCode::CodexConnectionLost ? AssistantOperation::Uncertain : AssistantOperation::Idle;
        if (auto* value = mutableMessage(m_pendingUser)) {
            value->delivery = m_operation == AssistantOperation::Uncertain ? AssistantDelivery::Uncertain : AssistantDelivery::NotSent;
            value->final = true; emit messageChanged(value->id);
        }
    } else if (method == "turn/start" && (m_operation == AssistantOperation::Submitting || m_operation == AssistantOperation::Cancelling)) {
        if (auto* value = mutableMessage(m_pendingUser)) { value->delivery = AssistantDelivery::Uncertain; value->final = true; emit messageChanged(value->id); }
        m_operation = AssistantOperation::Uncertain;
    } else if (method == "thread/read" && m_operation == AssistantOperation::Reconciling) m_operation = AssistantOperation::Idle;
    setError(error.code, error.userMessage);
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
}
