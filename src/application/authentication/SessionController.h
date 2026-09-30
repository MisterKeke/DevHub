#pragma once

#include "SessionState.h"
#include "../../domain/account/Account.h"

#include <QObject>

#include <optional>

namespace devhub::application::authentication {

class SessionController final : public QObject
{
    Q_OBJECT
public:
    explicit SessionController(QObject* parent = nullptr);

    [[nodiscard]] SessionState state() const noexcept { return m_state; }
    [[nodiscard]] const std::optional<domain::account::Account>& account() const noexcept { return m_account; }
    [[nodiscard]] bool keepSession() const noexcept { return m_keepSession; }

    void beginAccountCreation();
    void accountCreationFailed();
    void accountCreated(domain::account::Account account);
    void accountAvailable();
    void beginSignIn();
    void signInFailed();
    void signInSucceeded(domain::account::Account account, bool keepSession);
    void beginLock();
    void beginUnlock();
    void unlockFailed();
    void unlockSucceeded(domain::account::Account account);
    void signOut();
    void profileReset();
    void clear();

signals:
    void stateChanged(devhub::application::authentication::SessionState state);
    void keepSessionChanged(bool keepSession);

private:
    void setState(SessionState state);
    void setKeepSession(bool keepSession);

    SessionState m_state = SessionState::NoAccount;
    std::optional<domain::account::Account> m_account;
    bool m_keepSession = false;
};

} // namespace devhub::application::authentication
