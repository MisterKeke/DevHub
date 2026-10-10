#include "AssistantApprovalCoordinator.h"
#include <QThread>

namespace devhub::application::assistant {
AssistantApprovalCoordinator::AssistantApprovalCoordinator(QObject* parent) : QObject(parent) {
    m_clock.start(); m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, [this] { expireNow(m_clock.elapsed()); });
    m_timer.start();
}
QString AssistantApprovalCoordinator::request(ToolCall call, ToolCancellation cancellation,
    QString workspace, QString effect, QString preview, Decision decision) {
    Q_ASSERT(QThread::currentThread() == thread());
    const QString id = QString::number(++m_nextId);
    if (cancellation.isCancellationRequested() || !decision || m_pending.size() >= 2) {
        if (decision) decision(false);
        return {};
    }
    AssistantApprovalRequest value{id, call.name, std::move(workspace), std::move(effect), std::move(preview), std::move(call)};
    m_pending.insert(id, {std::move(value), std::move(cancellation), std::move(decision)});
    emit changed(); return id;
}
bool AssistantApprovalCoordinator::decide(const QString& id, bool allowOnce) {
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = m_pending.find(id); if (it == m_pending.end()) return false;
    auto entry = std::move(it.value()); m_pending.erase(it); emit changed();
    const bool valid = !entry.cancellation.isCancellationRequested()
        && (entry.value.call.expiresAtMonotonicMs <= 0 || m_clock.elapsed() < entry.value.call.expiresAtMonotonicMs);
    entry.decision(allowOnce && valid); return true;
}
void AssistantApprovalCoordinator::revokeAll() {
    const auto ids = m_pending.keys(); for (const auto& id : ids) (void)decide(id, false);
}
void AssistantApprovalCoordinator::revokeCall(const QString& callId) {
    const auto ids = m_pending.keys();
    for (const auto& id : ids) if (m_pending.value(id).value.call.callId == callId) (void)decide(id, false);
}
void AssistantApprovalCoordinator::expireNow(qint64 now) {
    const auto ids = m_pending.keys();
    for (const auto& id : ids) {
        const auto it = m_pending.constFind(id);
        if (it == m_pending.cend()) continue;
        const auto entry = it.value();
        if (entry.cancellation.isCancellationRequested()
            || (entry.value.call.expiresAtMonotonicMs > 0 && now >= entry.value.call.expiresAtMonotonicMs)) (void)decide(id, false);
    }
}
QList<AssistantApprovalRequest> AssistantApprovalCoordinator::pending() const {
    QList<AssistantApprovalRequest> result;
    for (const auto& entry : m_pending) result.append(entry.value);
    return result;
}
}
