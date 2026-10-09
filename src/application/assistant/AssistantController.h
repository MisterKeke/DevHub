#pragma once
#include "ICodexClient.h"
#include "IConversationRepository.h"
#include "shared/time/ApplicationClock.h"
#include "shared/identifiers/IdGenerator.h"
#include <QSet>
#include <QTimer>
#include <optional>

namespace devhub::application::assistant {
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
    void setSessionAllowed(bool allowed, bool clearTranscript = false);
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
private:
    void refreshConnection();
    void beginProbe();
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
    void applyPendingConfiguration();
    AssistantMessage* mutableMessage(const QUuid&);
    qsizetype transcriptBytes() const;
    ICodexClient& m_client;
    shared::time::IApplicationClock& m_clock;
    shared::identifiers::IIdGenerator& m_ids;
    IConversationRepository* m_repository;
    QString m_scratch;
    QStringList m_arguments;
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
    domain::errors::AppError m_error;
    QUuid m_pendingUser;
    QUuid m_retryMessage;
    QString m_model;
    QString m_runtimeVersion;
    QString m_loginStatus;
    AssistantConfiguration m_configuration;
    std::optional<AssistantConfiguration> m_pendingConfiguration;
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
    bool m_newChatWanted = false;
    bool m_contextBlocked = false;
    bool m_limitReached = false;
    bool m_modelsRequested = false;
    bool m_threadOwned = false;
};
}
