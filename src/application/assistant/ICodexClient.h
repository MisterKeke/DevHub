#pragma once
#include "AssistantTypes.h"
#include "shared/common/Result.h"
#include <QObject>

namespace devhub::application::assistant {
// Application-facing protocol port. Tests still exercise the concrete client
// through ICodexTransport; there is no second fake protocol implementation.
class ICodexClient : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual shared::common::Result<void> launch(const CodexLaunchSpec&) = 0;
    virtual void stop() = 0;
    virtual shared::common::Result<qint64> readAccount(bool refreshToken = false) = 0;
    virtual shared::common::Result<qint64> startLogin(CodexLoginMode mode) = 0;
    virtual shared::common::Result<qint64> cancelLogin() = 0;
    virtual shared::common::Result<qint64> listModels(const QString& cursor = {}) = 0;
    virtual shared::common::Result<qint64> startThread(const QString& model) = 0;
    virtual shared::common::Result<qint64> resumeThread(const CodexThreadBinding&) = 0;
    virtual shared::common::Result<qint64> readThread(const CodexThreadBinding&) = 0;
    virtual shared::common::Result<qint64> startTurn(const QString& text) = 0;
    virtual shared::common::Result<qint64> interruptTurn() = 0;
    virtual bool isInitialized() const = 0;
    virtual bool profileVerified() const = 0;
    virtual bool canStartConversation() const = 0;
    virtual bool canSend() const = 0;
    virtual CodexAccount account() const = 0;
    virtual CodexThreadBinding threadBinding() const = 0;
    virtual QString activeTurnId() const = 0;
signals:
    void initialized(CodexInitializeInfo info);
    void accountChanged(CodexAccount account);
    void loginChallenge(CodexLoginChallenge challenge);
    void loginCleared();
    void loginFinished(QString loginId, bool success);
    void modelsReceived(CodexModelPage page);
    void profileChecked(bool accepted, QStringList reasons);
    void threadReceived(CodexThread thread);
    void turnAccepted(QString threadId, QString turnId);
    void itemChanged(CodexItem item);
    void turnFinished(CodexTurn turn);
    void requestSucceeded(qint64 id, QString method);
    void requestFailed(qint64 id, QString method, devhub::domain::errors::AppError error);
    void protocolFailed(devhub::domain::errors::AppError error);
    void connectionClosed(quint64 generation);
    void backendStopped(quint64 generation);
};
}
