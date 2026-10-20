#pragma once
#include "application/assistant/ICodexTransport.h"
#include "application/assistant/ICodexClient.h"
#include "CodexWireContract.h"
#include "CodexBehaviorVerifier.h"
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <optional>

namespace devhub::infrastructure::codex {
class CodexClient final : public application::assistant::ICodexClient {
    Q_OBJECT
public:
    explicit CodexClient(application::assistant::ICodexTransport& transport, QObject* parent = nullptr,
        application::assistant::CodexClientLimits limits = {});
    [[nodiscard]] shared::common::Result<void> launch(const application::assistant::CodexLaunchSpec& spec) override;
    void stop() override;
    [[nodiscard]] shared::common::Result<qint64> readAccount(bool refreshToken = false) override;
    [[nodiscard]] shared::common::Result<qint64> startLogin(application::assistant::CodexLoginMode mode) override;
    [[nodiscard]] shared::common::Result<qint64> cancelLogin() override;
    [[nodiscard]] shared::common::Result<qint64> listModels(const QString& cursor = {}) override;
    [[nodiscard]] shared::common::Result<void> verifyRuntime();
    [[nodiscard]] shared::common::Result<qint64> startThread(const QString& model) override;
    [[nodiscard]] shared::common::Result<qint64> resumeThread(const application::assistant::CodexThreadBinding& binding) override;
    [[nodiscard]] shared::common::Result<qint64> readThread(const application::assistant::CodexThreadBinding& binding) override;
    [[nodiscard]] shared::common::Result<qint64> startTurn(const QString& text) override;
    [[nodiscard]] shared::common::Result<qint64> interruptTurn() override;
    [[nodiscard]] bool isInitialized() const override { return m_initialized; }
    [[nodiscard]] bool profileVerified() const override { return m_profileVerified; }
    [[nodiscard]] application::assistant::CodexCompatibilityReport compatibilityReport() const override { return m_compatibility; }
    bool verifyCompatibility() override;
    void cancelCompatibilityVerification() override { m_behaviorVerifier.cancel(); }
    [[nodiscard]] bool canSend() const override;
    [[nodiscard]] bool canStartConversation() const override;
    [[nodiscard]] int pendingRequestCount() const { return static_cast<int>(m_pending.size()); }
    [[nodiscard]] quint64 ignoredMessageCount() const { return m_ignored; }
    [[nodiscard]] application::assistant::CodexAccount account() const override
    { return m_accountRefreshPending && m_account.mode == application::assistant::CodexAccountMode::ChatGpt
        ? application::assistant::CodexAccount{} : m_account; }
    [[nodiscard]] application::assistant::CodexThreadBinding threadBinding() const override { return m_thread; }
    [[nodiscard]] QString activeTurnId() const override { return m_turnId; }
    [[nodiscard]] quint64 processGeneration() const override { return m_generation; }
    [[nodiscard]] bool configureDynamicTools(QJsonArray specifications) override;
    [[nodiscard]] bool resolveDynamicToolCall(const QJsonValue& requestId, const QJsonObject& response) override;
private:
    enum class Operation { Initialize, Account, LoginStart, LoginCancel, Models, Config, Requirements, Features, Hooks, Mcp, Plugins,
        ThreadStart, ThreadResume, ThreadRead, TurnStart, TurnInterrupt };
    struct Pending {
        Operation operation;
        qint64 deadline;
        quint64 generation;
        quint64 accountEpoch;
        QString expectedThread;
        QString argument;
    };
    [[nodiscard]] shared::common::Result<qint64> request(Operation operation, QJsonValue params,
        QString expectedThread = {}, QString argument = {});
    [[nodiscard]] bool hasPending(Operation operation) const;
    [[nodiscard]] bool bindingMatches(const application::assistant::CodexThreadBinding& binding) const;
    [[nodiscard]] bool matchesScratchDirectory(const QString& path) const;
    [[nodiscard]] bool liveInferenceGateOpen(bool revalidateFiles = false) const;
    void receive(const QJsonObject& object, quint64 generation);
    [[nodiscard]] bool scopeMessage(const QJsonObject& object) const;
    void response(const QJsonObject& object);
    void notification(const QJsonObject& object);
    void serverRequest(const QJsonObject& object);
    [[nodiscard]] bool decodeResult(qint64 id, const Pending& pending, const QJsonObject& result);
    [[nodiscard]] bool decodeThread(const QJsonObject& value, application::assistant::CodexThread& thread,
        bool allowUnassignedEnvironments, bool allowScratchEnvironment = false) const;
    [[nodiscard]] bool decodeTurn(const QJsonObject& value, const QString& threadId, application::assistant::CodexTurn& turn,
        bool live = false) const;
    [[nodiscard]] bool decodeItem(const QJsonObject& value, const QString& threadId, const QString& turnId,
        application::assistant::CodexItem& item) const;
    void turnEvent(const QJsonObject& object);
    [[nodiscard]] bool mergeItem(application::assistant::CodexItem item);
    void finishTurn(application::assistant::CodexTurn turn);
    void failPending(qint64 id, const Pending& pending, domain::errors::AppError error, bool uncertain);
    void disconnectClient(domain::errors::AppError error);
    void fatal(const QString& safeReason, quint64 expectedGeneration = 0);
    void deadlines();
    void clearLogin();
    void loginCompleted(const QJsonObject& params);
    void expireAccount();
    void finishProfileCheck();
    bool watchConfiguration(const QJsonObject& configResult);
    [[nodiscard]] QString configurationStamp() const;
    void invalidateCompatibility(application::assistant::CodexCompatibilityReason reason, const QString& explanation);
    static QString method(Operation operation);
    static QString responseContract(Operation operation);
    QPointer<application::assistant::ICodexTransport> m_transport;
    application::assistant::CodexClientLimits m_limits;
    CodexWireContract m_wire;
    CodexBehaviorVerifier m_behaviorVerifier;
    QFileSystemWatcher m_executableWatcher;
    QFileSystemWatcher m_configurationWatcher;
    QSet<QString> m_configurationPaths;
    QString m_configurationStamp;
    QSet<QString> m_sessionBehaviorEvidence;
    quint64 m_behaviorVerificationGeneration = 0;
    QHash<qint64, Pending> m_pending;
    QHash<QByteArray, QByteArray> m_serverRequests;
    QSet<QByteArray> m_resolvedServerRequests;
    QHash<QString, application::assistant::CodexItem> m_items;
    QStringList m_itemOrder;
    QList<QJsonObject> m_earlyTurns;
    QList<QJsonObject> m_earlyLogins;
    qsizetype m_earlyBytes = 0;
    // Outbound tool results share the same bounded account-verification queue.
    // Direction is internal state, never a field supplied by the native peer.
    QList<QPair<bool, QJsonObject>> m_accountDeferred;
    qsizetype m_accountDeferredBytes = 0;
    bool m_accountRefreshPending = false;
    qsizetype m_earlyLoginBytes = 0;
    QElapsedTimer m_clock;
    QTimer m_deadlineTimer;
    QTimer m_loginTimer;
    quint64 m_generation = 0;
    quint64 m_accountEpoch = 0;
    quint64 m_ignored = 0;
    qint64 m_nextId = 1;
    bool m_active = false;
    bool m_transportBusy = false;
    bool m_initializeSent = false;
    bool m_initialized = false;
    bool m_profileVerified = false;
    bool m_developmentLiveValidationAuthorized = false;
    bool m_uncertain = false;
    bool m_interruptAccepted = false;
    bool m_dynamicToolsEnabled = false;
    QJsonArray m_dynamicToolSpecifications;
    QString m_dynamicToolFingerprint;
    QString m_scratch;
    QString m_expectedHome;
    QString m_profileFingerprint;
    QString m_environmentIdentity;
    QStringList m_legacyProfileFingerprints;
    QStringList m_launchArguments;
    application::assistant::CodexInitializeInfo m_initializeInfo;
    application::assistant::CodexCompatibilityReport m_compatibility;
    bool m_runtimeInventoriesVerified = false;
    QSet<QString> m_inventoryReplies;
    QString m_inventoryFailure;
    std::optional<QJsonObject> m_mcpInventory;
    application::assistant::CodexAccount m_account;
    application::assistant::CodexThreadBinding m_thread;
    QString m_turnId;
    QString m_lastTurnId;
    std::optional<application::assistant::CodexLoginChallenge> m_login;
    std::optional<QJsonObject> m_config;
    std::optional<QJsonObject> m_requirements;
};
}
