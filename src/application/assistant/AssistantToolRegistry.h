#pragma once

#include "../../shared/common/Result.h"
#include <QJsonObject>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

namespace devhub::application::assistant {

class AssistantController;
class ICodexClient;

enum class ToolEffect { ReadOnly, ApprovedMutation, UnsupportedMutation };
enum class ToolAffinity { Worker, OwnerThread };

class ToolScope final
{
public:
    ToolScope() = default;
    ToolScope(QString scopeId, QString accountId, QString workspaceId, QString workspaceRoot,
              quint64 accessGeneration, quint64 processGeneration, bool authorized,
              QStringList explicitlySelectedNoteIds = {}, bool notesReadGrant = false,
              bool filesReadGrant = false, bool taskDataReadGrant = false);
    [[nodiscard]] const QString& scopeId() const { return m_scopeId; }
    [[nodiscard]] const QString& accountId() const { return m_accountId; }
    [[nodiscard]] const QString& workspaceId() const { return m_workspaceId; }
    [[nodiscard]] const QString& workspaceRoot() const { return m_workspaceRoot; }
    [[nodiscard]] quint64 accessGeneration() const { return m_accessGeneration; }
    [[nodiscard]] quint64 processGeneration() const { return m_processGeneration; }
    [[nodiscard]] bool authorized() const { return m_authorized; }
    [[nodiscard]] bool notesReadAllowed() const { return m_notesReadGrant || !m_explicitlySelectedNoteIds.isEmpty(); }
    [[nodiscard]] bool allowsNoteRead(const QString& noteId) const
    { return m_authorized && (m_notesReadGrant || m_explicitlySelectedNoteIds.contains(noteId)); }
    [[nodiscard]] bool filesReadAllowed() const { return m_authorized && m_filesReadGrant; }
    [[nodiscard]] bool taskDataReadAllowed() const { return m_authorized && m_taskDataReadGrant; }
private:
    QString m_scopeId, m_accountId, m_workspaceId, m_workspaceRoot;
    quint64 m_accessGeneration = 0, m_processGeneration = 0;
    bool m_authorized = false;
    QStringList m_explicitlySelectedNoteIds;
    bool m_notesReadGrant = false;
    bool m_filesReadGrant = false;
    bool m_taskDataReadGrant = false;
};

struct ToolDefinition final {
    QString name;
    QString description;
    QStringList allowedArguments;
    QStringList requiredArguments;
    ToolEffect effect = ToolEffect::ReadOnly;
    ToolAffinity affinity = ToolAffinity::Worker;
    qsizetype maximumOutputBytes = 16 * 1024;
    bool requiresWorkspace = false;
    bool requiresNoteReadDisclosure = false;
    bool requiresFileReadDisclosure = false;
    bool requiresTaskDataReadDisclosure = false;
    std::function<bool(const QJsonObject& input, QJsonObject* normalized)> validateArguments;
};

struct ToolCall final {
    QString routeId;
    QString callId;
    QString name;
    QJsonObject arguments;
    ToolScope scope;
    qint64 expiresAtMonotonicMs = 0;
};

struct ToolResult final {
    bool success = false;
    QJsonObject data;
    QString errorCode;
    bool truncated = false;
    [[nodiscard]] QByteArray compactJson() const;
};

struct ToolCancellation final {
    std::shared_ptr<std::atomic_bool> flag = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<std::atomic_bool> mutationStarted = std::make_shared<std::atomic_bool>(false);
    [[nodiscard]] bool isCancellationRequested() const { return flag->load(); }
    void cancel() const { flag->store(true); }
    void markMutationStarted() const { mutationStarted->store(true); }
};

class AssistantToolRegistry final : public QObject
{
    Q_OBJECT
public:
    using Handler = std::function<void(const ToolCall&, const ToolCancellation&, std::function<void(ToolResult)>)>;
    using Resolver = std::function<void(const QString& routeId, const ToolResult& result)>;

    explicit AssistantToolRegistry(Resolver resolver, int maximumConcurrent = 2, int maximumCalls = 32,
                                   QObject* parent = nullptr);
    [[nodiscard]] bool registerTool(ToolDefinition definition, Handler handler);
    [[nodiscard]] bool invoke(ToolCall call);
    void cancelRoute(const QString& routeId, QString reason = QStringLiteral("cancelled"));
    void cancelScope(const QString& scopeId, quint64 accessGeneration);
    void cancelScopesBefore(quint64 accessGeneration);
    void cancelProcess(quint64 processGeneration);
    void cancelAll(QString reason = QStringLiteral("scope_revoked"));
    void bindLifecycle(AssistantController& controller, ICodexClient& client);
    void expireNow(qint64 monotonicMilliseconds);
    [[nodiscard]] int activeCount() const { return static_cast<int>(m_activeDedupe.size()); }
    [[nodiscard]] qint64 monotonicMilliseconds() const { return m_clock.elapsed(); }
    [[nodiscard]] static QString schedulerModuleKey(const ToolCall& call);

signals:
    void activityStarted(QString callId, QString toolName);
    void activityFinished(QString callId, QString toolName, bool success);
    void mutationUncertain(QString toolName);

private:
    struct Entry final { ToolDefinition definition; Handler handler; };
    struct Active final { ToolCall call; ToolCancellation cancellation; bool resolved = false; };
    [[nodiscard]] QString dedupeKey(const ToolCall& call) const;
    [[nodiscard]] bool validate(const Entry& entry, const ToolCall& call, QJsonObject* normalized) const;
    [[nodiscard]] ToolResult capResult(const ToolResult& result, qsizetype cap) const;
    void resolveActive(const QString& routeId, ToolResult result, bool cancelWorker);
    void resolveRoute(const QString& routeId, quint64 processGeneration, ToolResult result);
    void cacheTerminal(const QString& key, ToolResult result);

    Resolver m_resolver;
    int m_maximumConcurrent = 2;
    int m_maximumCalls = 32;
    QHash<QString, Entry> m_definitions;
    QHash<QString, Active> m_active;
    QHash<QString, QString> m_activeDedupe;
    QHash<QString, ToolResult> m_terminalDedupe;
    QHash<QString, QByteArray> m_dedupeCallFingerprints;
    QQueue<QString> m_terminalOrder;
    QSet<QString> m_mutationKeys;
    QSet<QString> m_resolvedRouteKeys;
    QQueue<QString> m_resolvedRouteOrder;
    QTimer m_expiryTimer;
    QElapsedTimer m_clock;
    qint64 m_monotonicNow = 0;
};

} // namespace devhub::application::assistant
