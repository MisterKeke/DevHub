#pragma once
#include "ICodexClient.h"
#include "IConversationRepository.h"
#include "shared/time/ApplicationClock.h"
#include "shared/identifiers/IdGenerator.h"
#include <QSet>
#include <QTimer>
#include <functional>
#include <optional>

namespace devhub::application::assistant {
class AssistantApprovalCoordinator;
class AssistantController final : public QObject {
    Q_OBJECT
public:
    AssistantController(ICodexClient& client, shared::time::IApplicationClock& clock,
        shared::identifiers::IIdGenerator& ids, QString scratchDirectory, QStringList launchArguments,
        QProcessEnvironment environment, IConversationRepository* repository = nullptr,
        QObject* parent = nullptr, AssistantLimits limits = {});
    void open();
    void reconnect();
    void applyConfiguration(AssistantConfiguration configuration);
    void connectAccount(CodexLoginMode mode);
    void cancelAccountLogin();
    [[nodiscard]] bool canConnectAccount() const;
    [[nodiscard]] bool loginActive() const { return m_loginPending || m_challenge.has_value(); }
    [[nodiscard]] std::optional<CodexLoginChallenge> accountChallenge() const { return m_challenge; }
    [[nodiscard]] QString loginStatusText() const { return m_loginStatus; }
    [[nodiscard]] QString runtimeText() const;
    [[nodiscard]] bool canVerifyCompatibility() const;
    [[nodiscard]] bool compatibilityVerificationActive() const { return m_client.compatibilityReport().verificationInProgress; }
    void verifyCompatibility();
    void cancelCompatibilityVerification() { m_client.cancelCompatibilityVerification(); }
    [[nodiscard]] bool setupAllowed() const { return m_allowed; }
    [[nodiscard]] const QList<CodexModel>& models() const { return m_models; }
    [[nodiscard]] AssistantConfiguration configuration() const { return m_configuration; }
    void executableFound(CodexExecutable executable, quint64 probeGeneration);
    void probeFailed(domain::errors::AppError error, quint64 probeGeneration);
    bool send(const QString& text);
    void stopTurn();
    void newChat();
    void retry(const QUuid& messageId);
    void setContext(AssistantContext context);
    void setPersistenceContext(QString accountId, bool restoreEnabled);
    void setSessionAllowed(bool allowed, bool clearTranscript = false);
    void beginShutdown(std::function<void()> completion = {});
    [[nodiscard]] QStringList diagnosticsSnapshot() const;
    [[nodiscard]] quint64 accessGeneration() const noexcept { return m_accessGeneration; }
    [[nodiscard]] quint64 processGeneration() const noexcept { return m_processGeneration; }
    [[nodiscard]] bool accessAllowed() const noexcept { return m_allowed && !m_shuttingDown; }
    [[nodiscard]] const QString& accountId() const noexcept { return m_accountId; }
    void setToolDisclosureGrants(bool notes, bool files);
    void setTaskDataDisclosureGrant(bool granted);
    [[nodiscard]] bool notesDisclosureGranted() const noexcept { return m_notesDisclosureGranted; }
    [[nodiscard]] bool filesDisclosureGranted() const noexcept { return m_filesDisclosureGranted; }
    [[nodiscard]] bool taskDataDisclosureGranted() const noexcept { return m_taskDataDisclosureGranted; }
    void reportToolActivity(QString toolName, bool active, bool success = false);
    void setApprovalCoordinator(AssistantApprovalCoordinator* coordinator);
    [[nodiscard]] AssistantApprovalCoordinator* approvals() const { return m_approvals; }
    [[nodiscard]] bool canSend() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool canStop() const;
    [[nodiscard]] bool canNewChat() const;
    [[nodiscard]] bool canReconnect() const;
    [[nodiscard]] bool canRetry(const QUuid&) const;
    [[nodiscard]] bool retryIsNewTurn(const QUuid&) const;
    [[nodiscard]] AssistantConnectionState connection() const { return m_connection; }
    [[nodiscard]] AssistantOperation operation() const { return m_operation; }
    [[nodiscard]] AssistantConversation conversation() const { return m_conversation; }
    [[nodiscard]] const QList<AssistantMessage>& messages() const { return m_messages; }
    [[nodiscard]] const QList<AssistantConversationMetadata>& sessionConversations() const { return m_sessionConversations; }
    [[nodiscard]] const AssistantMessage* message(const QUuid&) const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString errorText() const { return m_error.userMessage; }
    [[nodiscard]] shared::common::Result<QList<AssistantConversationMetadata>> savedConversations(const QString& accountId) const;
signals:
    void executableProbeRequested(QString configuredPath, quint64 generation);
    void cancelProbeRequested();
    void stateChanged();
    void transcriptReset();
    void messageAdded(QUuid id);
    void messageChanged(QUuid id);
    void sendAdmitted(QString source);
    // Lifecycle contract for future capabilities: every scope revocation bumps
    // this generation before asynchronous work is interrupted or discarded.
    void accessRevoked(quint64 generation);
    void shutdownFinished();
    void toolDisclosureChanged(bool notes, bool files);
    void taskDataDisclosureChanged(bool granted);
    void toolActivityChanged(QString toolName, bool active, bool success);
    void approvalCoordinatorChanged();
    void toolMutationUncertain(QString toolName);
private:
    void refreshConnection();
    void beginProbe();
    void restartConnection();
    void submit();
    void admitPendingMessage();
    void receivedThread(const CodexThread&);
    void receivedItem(const CodexItem&);
    void finishedTurn(const CodexTurn&);
    void failedRequest(const QString&, const domain::errors::AppError&);
    void flushMessages();
    void resetConversation(bool retainAssociation = true);
    void setError(domain::errors::ErrorCode code, const QString& text);
    void exceededLimit();
    void selectModel();
    void maybeRestoreConversation();
    void persistCurrent(const QString& turnStatus = {});
    void hydrateConversation(const AssistantConversationMetadata&, const CodexThread&);
    void applyPendingConfiguration();
    void revokeAccess();
    void stopBackendAfterInterrupt();
    void completeShutdown();
    AssistantMessage* mutableMessage(const QUuid&);
    qsizetype transcriptBytes() const;
    ICodexClient& m_client;
    AssistantApprovalCoordinator* m_approvals = nullptr;
    shared::time::IApplicationClock& m_clock;
    shared::identifiers::IIdGenerator& m_ids;
    IConversationRepository* m_repository;
    QString m_scratch;
    QString m_accountId;
    QStringList m_arguments;
    QStringList m_baseArguments;
    QProcessEnvironment m_environment;
    AssistantLimits m_limits;
    AssistantConnectionState m_connection = AssistantConnectionState::Disconnected;
    AssistantOperation m_operation = AssistantOperation::Idle;
    AssistantConversation m_conversation;
    CodexThreadBinding m_binding;
    AssistantContext m_selectedContext;
    QList<AssistantMessage> m_messages;
    QList<AssistantConversationMetadata> m_sessionConversations;
    QHash<QString, QUuid> m_itemMessages;
    QSet<QUuid> m_changed;
    QSet<QUuid> m_reconciled;
    QTimer m_renderTimer;
    QTimer m_cancelTimer;
    QTimer m_startupTimer;
    domain::errors::AppError m_error;
    QUuid m_pendingUser;
    QUuid m_retryMessage;
    QString m_model;
    QDateTime m_conversationCreatedAtUtc;
    QString m_runtimeVersion;
    QStringList m_profileProblems;
    QString m_loginStatus;
    AssistantConfiguration m_configuration;
    std::optional<AssistantConfiguration> m_pendingConfiguration;
    std::optional<AssistantConversationMetadata> m_restoreMetadata;
    std::optional<CodexLoginChallenge> m_challenge;
    QList<CodexModel> m_models;
    bool m_loginPending = false;
    bool m_refreshModelsAfterLogin = false;
    quint64 m_probeGeneration = 0;
    quint64 m_processGeneration = 0;
    bool m_allowed = true;
    bool m_opened = false;
    bool m_backendOwned = false;
    bool m_reconnectWanted = false;
    bool m_profileRetryAttempted = false;
    bool m_newChatWanted = false;
    bool m_contextBlocked = false;
    bool m_limitReached = false;
    bool m_modelsRequested = false;
    bool m_threadOwned = false;
    bool m_restoreEnabled = true;
    bool m_restoreAttempted = false;
    bool m_autoRestoreSuppressed = false;
    bool m_restoreUnavailable = false;
    bool m_shuttingDown = false;
    bool m_shutdownStopRequested = false;
    bool m_shutdownComplete = false;
    bool m_notesDisclosureGranted = false;
    bool m_filesDisclosureGranted = false;
    bool m_taskDataDisclosureGranted = false;
    quint64 m_accessGeneration = 0;
    std::function<void()> m_shutdownCompletion;
};
}
