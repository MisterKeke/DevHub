#pragma once

#include "../../application/accounts/IAccountRepository.h"
#include "../../application/accounts/IUserDirectoryProvisioner.h"
#include "../../application/accounts/IProfileResetter.h"
#include "../../shared/identifiers/IdGenerator.h"
#include "../security/PasswordTaskService.h"

#include <QObject>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>

#include <functional>

namespace devhub::application::authentication {

class AuthenticationService final : public QObject
{
    Q_OBJECT
public:
    using Completion = std::function<void(shared::common::Result<domain::account::Account>)>;
    using VoidCompletion = std::function<void(shared::common::Result<void>)>;

    AuthenticationService(accounts::IAccountRepository& repository,
                          accounts::IUserDirectoryProvisioner& directoryProvisioner,
                          accounts::IProfileResetter& profileResetter,
                          shared::identifiers::IIdGenerator& idGenerator,
                          security::PasswordTaskService& passwordTasks,
                          QObject* parent = nullptr);

    void createAccount(QString username, QString password, QString confirmation, Completion completion);
    void signIn(QString username, QString password, Completion completion);
    void unlock(const domain::account::NormalizedUsername& username,
                QString password,
                Completion completion);
    void changePassword(const domain::account::NormalizedUsername& username,
                        QString currentPassword,
                        QString newPassword,
                        QString confirmation,
                        VoidCompletion completion);
    void resetProfile(QString typedConfirmation, VoidCompletion completion);
    [[nodiscard]] bool isBusy() const noexcept { return m_busy; }
    [[nodiscard]] int retryRemainingMs() const noexcept;

signals:
    void busyChanged(bool busy);
    void retryDelayChanged(int remainingMs);

private:
    void verify(const domain::account::NormalizedUsername& username,
                QString password,
                bool updateLastLogin,
                Completion completion);
    void setBusy(bool busy);
    void recordFailedAttempt();
    [[nodiscard]] static domain::errors::AppError invalidCredentialsError();
    [[nodiscard]] static domain::errors::AppError busyError();

    accounts::IAccountRepository& m_repository;
    accounts::IUserDirectoryProvisioner& m_directoryProvisioner;
    accounts::IProfileResetter& m_profileResetter;
    shared::identifiers::IIdGenerator& m_idGenerator;
    security::PasswordTaskService& m_passwordTasks;
    bool m_busy = false;
    int m_consecutiveFailures = 0;
    QElapsedTimer m_retryElapsed;
    int m_retryDurationMs = 0;
    QTimer m_retryTimer;
};

} // namespace devhub::application::authentication
