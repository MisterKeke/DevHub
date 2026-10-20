#pragma once
#include "AssistantToolRegistry.h"
#include <QHash>
#include <QTimer>

namespace devhub::application::assistant {
struct AssistantApprovalRequest final {
    QString id, tool, workspace, effect, preview;
    ToolCall call;
};

// Owner-thread, one-use approvals. The model never supplies decisions or grants.
class AssistantApprovalCoordinator final : public QObject {
    Q_OBJECT
public:
    using Decision = std::function<void(bool)>;
    explicit AssistantApprovalCoordinator(QObject* parent = nullptr);
    QString request(ToolCall call, ToolCancellation cancellation, QString workspace,
                    QString effect, QString preview, Decision decision);
    bool decide(const QString& id, bool allowOnce);
    void revokeAll();
    void revokeCall(const QString& callId);
    void revokeWorkspace(const QString& workspaceId);
    void expireNow(qint64 monotonicMs);
    [[nodiscard]] QList<AssistantApprovalRequest> pending() const;
signals:
    void changed();
private:
    struct Pending { AssistantApprovalRequest value; ToolCancellation cancellation; Decision decision; };
    QHash<QString, Pending> m_pending;
    QTimer m_timer;
    QElapsedTimer m_clock;
    quint64 m_nextId = 0;
};
}
