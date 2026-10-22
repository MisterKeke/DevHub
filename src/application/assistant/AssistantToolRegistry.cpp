#include "AssistantToolRegistry.h"
#include "AssistantController.h"
#include "ICodexClient.h"

#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QMetaObject>
#include <QJsonArray>
#include <algorithm>
#include <QThread>

namespace devhub::application::assistant {
namespace {
ToolResult failure(QString code)
{
    ToolResult result;
    result.errorCode = std::move(code);
    result.data.insert(QStringLiteral("error"), result.errorCode);
    return result;
}
}

ToolScope::ToolScope(QString scopeId, QString accountId, QString workspaceId, QString workspaceRoot,
                     quint64 accessGeneration, quint64 processGeneration, bool authorized,
                     QStringList explicitlySelectedNoteIds, bool notesReadGrant, bool filesReadGrant,
                     bool taskDataReadGrant)
    : m_scopeId(std::move(scopeId)), m_accountId(std::move(accountId)), m_workspaceId(std::move(workspaceId)),
      m_workspaceRoot(std::move(workspaceRoot)), m_accessGeneration(accessGeneration),
      m_processGeneration(processGeneration), m_authorized(authorized),
      m_explicitlySelectedNoteIds(std::move(explicitlySelectedNoteIds)), m_notesReadGrant(notesReadGrant),
      m_filesReadGrant(filesReadGrant), m_taskDataReadGrant(taskDataReadGrant) {}

QByteArray ToolResult::compactJson() const
{
    QJsonObject object{{QStringLiteral("success"), success}, {QStringLiteral("data"), data},
                       {QStringLiteral("truncated"), truncated}};
    if (!errorCode.isEmpty()) object.insert(QStringLiteral("errorCode"), errorCode);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

AssistantToolRegistry::AssistantToolRegistry(Resolver resolver, int maximumConcurrent, int maximumCalls, QObject* parent)
    : QObject(parent), m_resolver(std::move(resolver)), m_maximumConcurrent(qMax(1, maximumConcurrent)),
      m_maximumCalls(qMax(1, maximumCalls))
{
    m_clock.start();
    m_expiryTimer.setInterval(100);
    connect(&m_expiryTimer, &QTimer::timeout, this, [this] {
        expireNow(m_clock.elapsed());
    });
    m_expiryTimer.start();
}

bool AssistantToolRegistry::registerTool(ToolDefinition definition, Handler handler)
{
    Q_ASSERT(QThread::currentThread() == thread());
    static const QRegularExpression safeName(QStringLiteral("^[a-z][a-z0-9_]{0,63}$"));
    if (!safeName.match(definition.name).hasMatch() || definition.description.trimmed().isEmpty()
        || !handler || definition.effect == ToolEffect::UnsupportedMutation || definition.maximumOutputBytes < 128
        || definition.maximumOutputBytes > 256 * 1024 || m_definitions.contains(definition.name)) return false;
    if (!definition.allowedArguments.isEmpty() && !definition.validateArguments) return false;
    for (const auto& key : definition.allowedArguments) {
        const QString lower = key.toLower();
        if (key.isEmpty() || key.contains(QLatin1Char('.')) || key.contains(QLatin1Char(':'))
            || lower == QStringLiteral("accountid") || lower == QStringLiteral("account_id")
            || lower == QStringLiteral("root") || lower == QStringLiteral("workspaceroot")
            || lower == QStringLiteral("command") || lower == QStringLiteral("commandstring")
            || lower == QStringLiteral("shell") || lower == QStringLiteral("argv")
            || lower == QStringLiteral("executable") || lower == QStringLiteral("effect")) return false;
    }
    for (const auto& key : definition.requiredArguments) if (!definition.allowedArguments.contains(key)) return false;
    const QString registeredName = definition.name;
    m_definitions.insert(registeredName, Entry{std::move(definition), std::move(handler)});
    return true;
}

QString AssistantToolRegistry::dedupeKey(const ToolCall& call) const
{
    return QString::number(call.scope.processGeneration()) + QLatin1Char('|') + call.scope.scopeId()
        + QLatin1Char('|') + QString::number(call.scope.accessGeneration()) + QLatin1Char('|') + call.callId;
}

QString AssistantToolRegistry::schedulerModuleKey(const ToolCall& call)
{
    const QByteArray identity = (QString::number(call.scope.processGeneration()) + QLatin1Char('|')
        + call.scope.scopeId() + QLatin1Char('|') + QString::number(call.scope.accessGeneration())
        + QLatin1Char('|') + call.callId).toUtf8();
    const QString suffix = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(24));
    return QStringLiteral("assistant-tool-") + suffix;
}

bool AssistantToolRegistry::validate(const Entry& entry, const ToolCall& call, QJsonObject* normalized) const
{
    for (const QString& key : call.arguments.keys()) if (!entry.definition.allowedArguments.contains(key)) return false;
    for (const QString& key : entry.definition.requiredArguments) if (!call.arguments.contains(key)) return false;
    const auto forbidden = [](const QString& key) {
        const QString lower = key.toLower();
        return lower == QStringLiteral("accountid") || lower == QStringLiteral("account_id")
            || lower == QStringLiteral("account") || lower == QStringLiteral("root")
            || lower == QStringLiteral("rootpath") || lower == QStringLiteral("workspaceroot")
            || lower == QStringLiteral("workspacerootpath") || lower == QStringLiteral("command")
            || lower == QStringLiteral("commandstring") || lower == QStringLiteral("shell")
            || lower == QStringLiteral("argv") || lower == QStringLiteral("effect")
            || lower == QStringLiteral("executable");
    };
    std::function<bool(const QJsonValue&)> safeValue = [&](const QJsonValue& value) {
        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            for (const QString& key : object.keys()) if (forbidden(key) || !safeValue(object.value(key))) return false;
        } else if (value.isArray()) {
            for (const QJsonValue& child : value.toArray()) if (!safeValue(child)) return false;
        }
        return true;
    };
    for (const auto& key : call.arguments.keys()) if (forbidden(key) || !safeValue(call.arguments.value(key))) return false;
    *normalized = call.arguments;
    if (entry.definition.validateArguments && !entry.definition.validateArguments(call.arguments, normalized)) return false;
    for (const QString& key : normalized->keys()) if (!entry.definition.allowedArguments.contains(key)) return false;
    for (const auto& key : normalized->keys()) if (forbidden(key) || !safeValue(normalized->value(key))) return false;
    return true;
}

ToolResult AssistantToolRegistry::capResult(const ToolResult& result, qsizetype cap) const
{
    if (result.compactJson().size() <= cap) return result;
    ToolResult bounded;
    bounded.success = result.success;
    bounded.truncated = true;
    bounded.errorCode = result.success ? QString{} : result.errorCode;
    bounded.data.insert(QStringLiteral("truncated"), true);
    bounded.data.insert(QStringLiteral("reason"), QStringLiteral("output_limit"));
    if (bounded.compactJson().size() > cap) return failure(QStringLiteral("output_limit"));
    return bounded;
}

bool AssistantToolRegistry::invoke(ToolCall call)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (call.routeId.isEmpty() || call.routeId.size() > 128 || call.callId.isEmpty() || call.callId.size() > 128 || call.name.size() > 64
        || QJsonDocument(call.arguments).toJson(QJsonDocument::Compact).size() > 64 * 1024) return false;
    const QString routeKey = QString::number(call.scope.processGeneration()) + QLatin1Char('|') + call.routeId;
    if (m_resolvedRouteKeys.contains(routeKey) || m_active.contains(call.routeId)) return true;
    if (!call.scope.authorized()) { resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("scope_denied"))); return true; }
    const auto entry = m_definitions.constFind(call.name);
    if (entry == m_definitions.cend()) { resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("unknown_tool"))); return true; }
    QJsonObject normalized;
    if (!validate(*entry, call, &normalized)) { resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("invalid_arguments"))); return true; }
    if (call.scope.accountId().isEmpty() || call.scope.scopeId().isEmpty() || call.scope.processGeneration() == 0
        || (entry->definition.requiresWorkspace && (call.scope.workspaceId().isEmpty() || call.scope.workspaceRoot().isEmpty()))
        || (entry->definition.requiresNoteReadDisclosure && !call.scope.notesReadAllowed())) {
        resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("scope_denied"))); return true;
    }
    if (entry->definition.requiresFileReadDisclosure && !call.scope.filesReadAllowed()) {
        resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("scope_denied"))); return true;
    }
    if (entry->definition.requiresTaskDataReadDisclosure && !call.scope.taskDataReadAllowed()) {
        resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("scope_denied"))); return true;
    }
    const QString key = dedupeKey(call);
    const QByteArray callFingerprint = QCryptographicHash::hash(QJsonDocument(
        QJsonArray{call.name, call.arguments}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    const auto priorFingerprint = m_dedupeCallFingerprints.constFind(key);
    if (priorFingerprint != m_dedupeCallFingerprints.cend() && priorFingerprint.value() != callFingerprint) {
        resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("duplicate_conflict")));
        return true;
    }
    m_dedupeCallFingerprints.insert(key, callFingerprint);
    if (m_terminalDedupe.contains(key)) { resolveRoute(call.routeId, call.scope.processGeneration(), m_terminalDedupe.value(key)); return true; }
    if (m_activeDedupe.contains(key)) {
        // A duplicate server route receives the existing in-flight terminal result through its own route.
        if (m_active.size() >= m_maximumCalls) {
            resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("busy")));
            return true;
        }
        const QString original = m_activeDedupe.value(key);
        if (m_active.contains(original)) {
            const QString duplicateRoute = call.routeId;
            // Store duplicate route as an alias to the active call; it is resolved with the same result.
            Active duplicateActive = m_active.value(original);
            duplicateActive.call.routeId = duplicateRoute;
            m_active.insert(duplicateRoute, std::move(duplicateActive));
        }
        return true;
    }
    if (m_activeDedupe.size() >= m_maximumConcurrent || m_active.size() >= m_maximumCalls) {
        resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("busy"))); return true;
    }
    // Keep mutation outcomes for the entire runtime. Refuse new mutations at
    // the budget rather than evicting an identity that could execute again.
    if (entry->definition.effect == ToolEffect::ApprovedMutation) {
        if (m_mutationKeys.size() >= 256) {
            resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("mutation_budget"))); return true;
        }
        m_mutationKeys.insert(key);
    }
    if (call.expiresAtMonotonicMs > 0 && call.expiresAtMonotonicMs <= m_monotonicNow) {
        resolveRoute(call.routeId, call.scope.processGeneration(), failure(QStringLiteral("expired"))); return true;
    }
    call.arguments = std::move(normalized);
    Active active{call, {}, false};
    m_active.insert(call.routeId, active);
    m_activeDedupe.insert(key, call.routeId);
    emit activityStarted(call.callId, call.name);
    // Activity observers may synchronously lock/revoke scope. Do not enter a
    // handler after that revocation, even if its immutable call was admitted.
    if (!m_active.contains(call.routeId) || active.cancellation.isCancellationRequested()) return true;
    const QString routeId = call.routeId;
    const auto handler = entry->handler;
    QPointer<AssistantToolRegistry> registry(this);
    handler(call, active.cancellation, [registry, routeId, key, cap = entry->definition.maximumOutputBytes](ToolResult result) {
        if (!registry) return;
        QMetaObject::invokeMethod(registry, [registry, routeId, key, cap, result = std::move(result)]() mutable {
            if (!registry || !registry->m_active.contains(routeId)) return;
            const QString name = registry->m_active.value(routeId).call.name;
            const QString callId = registry->m_active.value(routeId).call.callId;
            ToolResult bounded = registry->capResult(result, cap);
            registry->cacheTerminal(key, bounded);
            const auto routes = registry->m_active.keys();
            for (const auto& activeRoute : routes) {
                if (registry->dedupeKey(registry->m_active.value(activeRoute).call) == key)
                    registry->resolveActive(activeRoute, bounded, false);
            }
            emit registry->activityFinished(callId, name, bounded.success);
            if (bounded.data.value(QStringLiteral("outcome")) == QStringLiteral("uncertain")) emit registry->mutationUncertain(name);
        }, Qt::AutoConnection);
    });
    return true;
}

void AssistantToolRegistry::cacheTerminal(const QString& key, ToolResult result)
{
    if (!m_terminalDedupe.contains(key)) m_terminalOrder.enqueue(key);
    m_terminalDedupe.insert(key, std::move(result));
    while (m_terminalOrder.size() > 256) {
        const auto expiredKey = m_terminalOrder.dequeue();
        if (!m_mutationKeys.contains(expiredKey)) {
            m_terminalDedupe.remove(expiredKey);
            m_dedupeCallFingerprints.remove(expiredKey);
        }
    }
}

void AssistantToolRegistry::resolveActive(const QString& routeId, ToolResult result, bool cancelWorker)
{
    if (!m_active.contains(routeId)) return;
    const Active active = m_active.take(routeId);
    const QString key = dedupeKey(active.call);
    if (cancelWorker) {
        active.cancellation.cancel();
        if (active.cancellation.mutationStarted->load()) {
            result.data.insert(QStringLiteral("outcome"), QStringLiteral("uncertain"));
            result.data.insert(QStringLiteral("retryAutomatically"), false);
        }
        cacheTerminal(key, result);
        // Aliases belong to the same invocation and must terminate together.
        const auto routes = m_active.keys();
        for (const auto& alias : routes)
            if (dedupeKey(m_active.value(alias).call) == key) resolveActive(alias, result, false);
    }
    resolveRoute(routeId, active.call.scope.processGeneration(),
        capResult(result, m_definitions.value(active.call.name).definition.maximumOutputBytes));
    if (cancelWorker) emit activityFinished(active.call.callId, active.call.name, false);
    if (cancelWorker && active.cancellation.mutationStarted->load()) emit mutationUncertain(active.call.name);
    if (!m_activeDedupe.contains(key)) return;
    const QString owner = m_activeDedupe.value(key);
    if (!m_active.contains(owner)) m_activeDedupe.remove(key);
}

void AssistantToolRegistry::resolveRoute(const QString& routeId, quint64 processGeneration, ToolResult result)
{
    const QString key = QString::number(processGeneration) + QLatin1Char('|') + routeId;
    if (m_resolvedRouteKeys.contains(key)) return;
    m_resolvedRouteKeys.insert(key);
    m_resolvedRouteOrder.enqueue(key);
    while (m_resolvedRouteOrder.size() > 512) m_resolvedRouteKeys.remove(m_resolvedRouteOrder.dequeue());
    if (m_resolver) m_resolver(routeId, result);
}

void AssistantToolRegistry::cancelRoute(const QString& routeId, QString reason)
{
    Q_ASSERT(QThread::currentThread() == thread());
    resolveActive(routeId, failure(reason.isEmpty() ? QStringLiteral("cancelled") : reason), true);
}
void AssistantToolRegistry::cancelScope(const QString& scopeId, quint64 accessGeneration)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto routes = m_active.keys();
    for (const auto& route : routes) {
        const auto call = m_active.value(route).call;
        if (call.scope.scopeId() == scopeId && call.scope.accessGeneration() == accessGeneration)
            resolveActive(route, failure(QStringLiteral("scope_revoked")), true);
    }
}
void AssistantToolRegistry::cancelScopesBefore(quint64 accessGeneration)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto routes = m_active.keys();
    for (const auto& route : routes) if (m_active.value(route).call.scope.accessGeneration() < accessGeneration)
        resolveActive(route, failure(QStringLiteral("scope_revoked")), true);
}
void AssistantToolRegistry::cancelProcess(quint64 processGeneration)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto routes = m_active.keys();
    for (const auto& route : routes) if (m_active.value(route).call.scope.processGeneration() == processGeneration)
        resolveActive(route, failure(QStringLiteral("process_lost")), true);
}
void AssistantToolRegistry::cancelAll(QString reason)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto routes = m_active.keys();
    for (const auto& route : routes) resolveActive(route, failure(reason), true);
}
void AssistantToolRegistry::bindLifecycle(AssistantController& controller, ICodexClient& client)
{
    Q_ASSERT(QThread::currentThread() == thread());
    connect(&controller, &AssistantController::accessRevoked, this, [this](quint64 generation) {
        cancelScopesBefore(generation);
    });
    connect(&client, &ICodexClient::connectionClosed, this, [this](quint64 generation) {
        cancelProcess(generation);
    });
    connect(&client, &ICodexClient::backendStopped, this, [this](quint64 generation) {
        cancelProcess(generation);
    });
}
void AssistantToolRegistry::expireNow(qint64 now)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_monotonicNow = now;
    const auto routes = m_active.keys();
    for (const auto& route : routes) {
        const auto call = m_active.value(route).call;
        if (call.expiresAtMonotonicMs > 0 && call.expiresAtMonotonicMs <= now)
            resolveActive(route, failure(QStringLiteral("expired")), true);
    }
}

} // namespace devhub::application::assistant
