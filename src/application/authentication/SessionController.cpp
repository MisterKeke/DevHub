#include "SessionController.h"
#include "AuthenticationProductPolicy.h"

#include <QThread>

#include <utility>

namespace devhub::application::authentication {

SessionController::SessionController(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<SessionState>();
}

void SessionController::beginAccountCreation()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == SessionState::NoAccount) {
        setState(SessionState::CreatingAccount);
    }
}

void SessionController::accountCreationFailed()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == SessionState::CreatingAccount) {
        setState(SessionState::NoAccount);
    }
}

void SessionController::accountCreated(domain::account::Account account)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != SessionState::CreatingAccount) {
        return;
    }
    m_account.emplace(std::move(account));
    setKeepSession(AuthenticationProductPolicy::KeepSessionDefault);
    setState(SessionState::SignedIn);
}

void SessionController::accountAvailable()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == SessionState::NoAccount) {
        setState(SessionState::SignedOut);
    }
}

void SessionController::beginSignIn()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == SessionState::SignedOut) {
        setState(SessionState::Authenticating);
    }
}

void SessionController::signInFailed()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == SessionState::Authenticating) {
        setState(SessionState::SignedOut);
    }
}

void SessionController::signInSucceeded(domain::account::Account account, bool keepSession)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != SessionState::Authenticating) {
        return;
    }
    m_account.emplace(std::move(account));
    setKeepSession(keepSession);
    setState(SessionState::SignedIn);
}

void SessionController::beginLock()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != SessionState::SignedIn) {
        return;
    }
    setState(SessionState::Locking);
    setState(SessionState::Locked);
}

void SessionController::beginUnlock()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == SessionState::Locked) {
        setState(SessionState::Unlocking);
    }
}

void SessionController::unlockFailed()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == SessionState::Unlocking) {
        setState(SessionState::Locked);
    }
}

void SessionController::unlockSucceeded(domain::account::Account account)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == SessionState::Unlocking && m_account.has_value()) {
        m_account.emplace(std::move(account));
        setState(SessionState::SignedIn);
    }
}

void SessionController::signOut()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != SessionState::SignedIn && m_state != SessionState::Locked) {
        return;
    }
    setState(SessionState::SigningOut);
    m_account.reset();
    setKeepSession(false);
    setState(SessionState::SignedOut);
}

void SessionController::profileReset()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_account.reset();
    setKeepSession(false);
    setState(SessionState::NoAccount);
}

void SessionController::clear()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_account.reset();
    setKeepSession(false);
    setState(SessionState::Cleared);
}

void SessionController::setState(SessionState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

void SessionController::setKeepSession(bool keepSession)
{
    if (m_keepSession == keepSession) {
        return;
    }
    m_keepSession = keepSession;
    emit keepSessionChanged(m_keepSession);
}

} // namespace devhub::application::authentication
