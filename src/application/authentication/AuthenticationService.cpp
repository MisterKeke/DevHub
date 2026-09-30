#include "AuthenticationService.h"

#include "AuthenticationProductPolicy.h"
#include "AuthenticationValidation.h"

#include <QPointer>
#include <QThread>

#include <algorithm>
#include <memory>
#include <utility>

namespace devhub::application::authentication {

AuthenticationService::AuthenticationService(accounts::IAccountRepository& repository,
                                             accounts::IUserDirectoryProvisioner& directoryProvisioner,
                                             accounts::IProfileResetter& profileResetter,
                                             shared::identifiers::IIdGenerator& idGenerator,
                                             security::PasswordTaskService& passwordTasks,
                                             QObject* parent)
    : QObject(parent),
      m_repository(repository),
      m_directoryProvisioner(directoryProvisioner),
      m_profileResetter(profileResetter),
      m_idGenerator(idGenerator),
      m_passwordTasks(passwordTasks)
{
    m_retryTimer.setInterval(250);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        const int remaining = retryRemainingMs();
        emit retryDelayChanged(remaining);
        if (remaining == 0) {
            m_retryTimer.stop();
        }
    });
}

void AuthenticationService::createAccount(QString username,
                                          QString password,
                                          QString confirmation,
                                          Completion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_busy) {
        completion(shared::common::Result<domain::account::Account>::failure(busyError()));
        return;
    }
    auto validation = validateCreateAccountInput(username, password, confirmation);
    if (!validation.isValid()) {
        completion(shared::common::Result<domain::account::Account>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument,
            domain::errors::ErrorCategory::Authentication,
            validation.message,
            QStringLiteral("Account creation input validation failed."),
            QStringLiteral("validate account creation"))));
        return;
    }

    setBusy(true);
    const auto displayUsername = *validation.displayUsername;
    const auto normalizedUsername = *validation.normalizedUsername;
    QPointer<AuthenticationService> service(this);
    [[maybe_unused]] const auto hashTask = m_passwordTasks.hashPassword(std::move(password),
        [service, displayUsername, normalizedUsername,
         completion = std::move(completion)](auto outcome) mutable {
            if (!service) {
                return;
            }
            service->setBusy(false);
            auto* result = std::get_if<shared::common::Result<domain::account::PasswordVerifier>>(&outcome);
            if (result == nullptr) {
                completion(shared::common::Result<domain::account::Account>::failure(
                    domain::errors::AppError::failure(
                        domain::errors::ErrorCode::OperationCancelled,
                        domain::errors::ErrorCategory::Authentication,
                        QStringLiteral("Account creation was cancelled."),
                        QStringLiteral("The account password task was cancelled."),
                        QStringLiteral("create local account"))));
                return;
            }
            if (!*result) {
                completion(shared::common::Result<domain::account::Account>::failure(result->error()));
                return;
            }
            auto id = domain::account::AccountId::create(service->m_idGenerator);
            if (!id) {
                completion(shared::common::Result<domain::account::Account>::failure(id.error()));
                return;
            }
            auto directories = service->m_directoryProvisioner.createInitialDirectories(id.value());
            if (!directories) {
                completion(shared::common::Result<domain::account::Account>::failure(directories.error()));
                return;
            }
            const domain::account::AccountId provisionedId = id.value();
            auto created = service->m_repository.createAccountWithId(
                std::move(id).value(), displayUsername, normalizedUsername, std::move(*result).value());
            if (!created) {
                auto directoryRollback = service->m_directoryProvisioner.removeUserDirectory(provisionedId);
                if (!directoryRollback) {
                    completion(shared::common::Result<domain::account::Account>::failure(
                        domain::errors::AppError::failure(
                            domain::errors::ErrorCode::InternalFailure,
                            domain::errors::ErrorCategory::FileSystem,
                            QStringLiteral("DevHub could not finish creating the account."),
                            QStringLiteral("Account storage failed and its staged user folder could not be removed."),
                            QStringLiteral("roll back account folders"))));
                    return;
                }
                completion(shared::common::Result<domain::account::Account>::failure(created.error()));
                return;
            }
            completion(shared::common::Result<domain::account::Account>::success(std::move(created).value()));
        });
}

void AuthenticationService::signIn(QString username,
                                   QString password,
                                   Completion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_busy) {
        completion(shared::common::Result<domain::account::Account>::failure(busyError()));
        return;
    }
    const int retryRemaining = retryRemainingMs();
    if (retryRemaining > 0) {
        completion(shared::common::Result<domain::account::Account>::failure(invalidCredentialsError()));
        return;
    }
    auto validation = validateSignInInput(username, password);
    if (!validation.isValid()) {
        recordFailedAttempt();
        completion(shared::common::Result<domain::account::Account>::failure(invalidCredentialsError()));
        return;
    }
    verify(*validation.normalizedUsername, std::move(password), true, std::move(completion));
}

void AuthenticationService::unlock(const domain::account::NormalizedUsername& username,
                                   QString password,
                                   Completion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_busy) {
        completion(shared::common::Result<domain::account::Account>::failure(busyError()));
        return;
    }
    if (retryRemainingMs() > 0) {
        completion(shared::common::Result<domain::account::Account>::failure(invalidCredentialsError()));
        return;
    }
    if (!validateUnlockInput(password).valid) {
        completion(shared::common::Result<domain::account::Account>::failure(invalidCredentialsError()));
        return;
    }
    verify(username, std::move(password), true, std::move(completion));
}

void AuthenticationService::changePassword(const domain::account::NormalizedUsername& username,
                                           QString currentPassword,
                                           QString newPassword,
                                           QString confirmation,
                                           VoidCompletion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_busy) {
        completion(shared::common::Result<void>::failure(busyError()));
        return;
    }
    if (currentPassword.isEmpty() || newPassword.isEmpty() || confirmation.isEmpty()) {
        completion(shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument,
            domain::errors::ErrorCategory::Authentication,
            QStringLiteral("Fill in each password field."),
            QStringLiteral("Password change requires the current, new, and confirmation fields."),
            QStringLiteral("validate password change"))));
        return;
    }
    if (newPassword != confirmation) {
        completion(shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument,
            domain::errors::ErrorCategory::Authentication,
            QStringLiteral("The new passwords do not match."),
            QStringLiteral("Password change confirmation did not match."),
            QStringLiteral("validate password change"))));
        return;
    }
    auto found = m_repository.findByUsername(username);
    if (!found) {
        completion(shared::common::Result<void>::failure(found.error()));
        return;
    }
    if (!found.value().has_value()) {
        completion(shared::common::Result<void>::failure(invalidCredentialsError()));
        return;
    }
    auto credentials = std::make_shared<accounts::AccountCredentials>(std::move(*found.value()));
    auto newPasswordHolder = std::make_shared<QString>(std::move(newPassword));
    QPointer<AuthenticationService> service(this);
    m_busy = true;
    emit busyChanged(true);
    [[maybe_unused]] const auto verifyTask = m_passwordTasks.verifyPassword(
        std::move(currentPassword), std::move(credentials->passwordVerifier),
        [service, credentials, newPasswordHolder,
         completion = std::move(completion)](auto outcome) mutable {
            if (!service) {
                newPasswordHolder->fill(QChar(u'\0'));
                newPasswordHolder->clear();
                return;
            }
            auto* result = std::get_if<shared::common::Result<security::PasswordVerificationResult>>(&outcome);
            if (result == nullptr) {
                service->setBusy(false);
                newPasswordHolder->fill(QChar(u'\0'));
                newPasswordHolder->clear();
                completion(shared::common::Result<void>::failure(domain::errors::AppError::failure(
                    domain::errors::ErrorCode::OperationCancelled,
                    domain::errors::ErrorCategory::Authentication,
                    QStringLiteral("Password change was cancelled."),
                    QStringLiteral("The current-password verification was cancelled."),
                    QStringLiteral("change local account password"))));
                return;
            }
            if (!*result) {
                service->setBusy(false);
                newPasswordHolder->fill(QChar(u'\0'));
                newPasswordHolder->clear();
                completion(shared::common::Result<void>::failure(result->error()));
                return;
            }
            if (!result->value().verified) {
                service->setBusy(false);
                newPasswordHolder->fill(QChar(u'\0'));
                newPasswordHolder->clear();
                completion(shared::common::Result<void>::failure(invalidCredentialsError()));
                return;
            }
            [[maybe_unused]] const auto hashTask = service->m_passwordTasks.hashPassword(
                std::move(*newPasswordHolder),
                [service, credentials, newPasswordHolder,
                 completion = std::move(completion)](auto hashOutcome) mutable {
                    if (!service) {
                        newPasswordHolder->fill(QChar(u'\0'));
                        newPasswordHolder->clear();
                        return;
                    }
                    service->setBusy(false);
                    auto* hashResult = std::get_if<shared::common::Result<domain::account::PasswordVerifier>>(
                        &hashOutcome);
                    if (hashResult == nullptr) {
                        completion(shared::common::Result<void>::failure(domain::errors::AppError::failure(
                            domain::errors::ErrorCode::OperationCancelled,
                            domain::errors::ErrorCategory::Authentication,
                            QStringLiteral("Password change was cancelled."),
                            QStringLiteral("The new-password hashing task was cancelled."),
                            QStringLiteral("change local account password"))));
                        return;
                    }
                    if (!*hashResult) {
                        completion(shared::common::Result<void>::failure(hashResult->error()));
                        return;
                    }
                    auto stored = service->m_repository.replacePasswordVerifier(
                        credentials->account.id(), std::move(*hashResult).value(),
                        QDateTime::currentDateTimeUtc());
                    completion(std::move(stored));
                });
        });
}

void AuthenticationService::resetProfile(QString typedConfirmation, VoidCompletion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_busy) {
        completion(shared::common::Result<void>::failure(busyError()));
        return;
    }
    if (typedConfirmation != QStringLiteral("RESET")) {
        completion(shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument,
            domain::errors::ErrorCategory::Authentication,
            QStringLiteral("Type RESET exactly to confirm."),
            QStringLiteral("Profile reset confirmation text did not match."),
            QStringLiteral("confirm local profile reset"))));
        return;
    }
    auto account = m_repository.findSingleAccount();
    if (!account) {
        completion(shared::common::Result<void>::failure(account.error()));
        return;
    }
    if (!account.value().has_value()) {
        completion(shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::AccountNotFound,
            domain::errors::ErrorCategory::Authentication,
            QStringLiteral("There is no local account to reset."),
            QStringLiteral("The single-account lookup returned no account."),
            QStringLiteral("reset local profile"))));
        return;
    }
    setBusy(true);
    auto reset = m_profileResetter.resetProfile(account.value()->id());
    setBusy(false);
    completion(std::move(reset));
}

int AuthenticationService::retryRemainingMs() const noexcept
{
    if (!m_retryElapsed.isValid()) {
        return 0;
    }
    return std::max(0, m_retryDurationMs - static_cast<int>(m_retryElapsed.elapsed()));
}

void AuthenticationService::verify(const domain::account::NormalizedUsername& username,
                                   QString password,
                                   bool updateLastLogin,
                                   Completion completion)
{
    setBusy(true);
    auto found = m_repository.findByUsername(username);
    if (!found) {
        setBusy(false);
        completion(shared::common::Result<domain::account::Account>::failure(found.error()));
        return;
    }
    if (!found.value().has_value()) {
        setBusy(false);
        recordFailedAttempt();
        completion(shared::common::Result<domain::account::Account>::failure(invalidCredentialsError()));
        return;
    }

    auto credentials = std::make_shared<accounts::AccountCredentials>(std::move(*found.value()));
    QPointer<AuthenticationService> service(this);
    [[maybe_unused]] const auto verifyTask = m_passwordTasks.verifyPassword(
        std::move(password), std::move(credentials->passwordVerifier),
        [service, credentials, updateLastLogin, completion = std::move(completion)](auto outcome) mutable {
            if (!service) {
                return;
            }
            service->setBusy(false);
            auto* result = std::get_if<shared::common::Result<security::PasswordVerificationResult>>(&outcome);
            if (result == nullptr) {
                completion(shared::common::Result<domain::account::Account>::failure(
                    domain::errors::AppError::failure(
                        domain::errors::ErrorCode::OperationCancelled,
                        domain::errors::ErrorCategory::Authentication,
                        QStringLiteral("Sign-in was cancelled."),
                        QStringLiteral("The password verification task was cancelled."),
                        QStringLiteral("verify local account"))));
                return;
            }
            if (!*result) {
                completion(shared::common::Result<domain::account::Account>::failure(result->error()));
                return;
            }
            if (!result->value().verified) {
                service->recordFailedAttempt();
                completion(shared::common::Result<domain::account::Account>::failure(
                    invalidCredentialsError()));
                return;
            }
            service->m_consecutiveFailures = 0;
            service->m_retryElapsed.invalidate();
            service->m_retryDurationMs = 0;
            service->m_retryTimer.stop();
            emit service->retryDelayChanged(0);
            if (updateLastLogin) {
                auto recorded = service->m_repository.recordSuccessfulLogin(
                    credentials->account.id(), QDateTime::currentDateTimeUtc(),
                    std::move(result->value().replacementVerifier));
                if (!recorded) {
                    completion(shared::common::Result<domain::account::Account>::failure(recorded.error()));
                    return;
                }
            }
            completion(shared::common::Result<domain::account::Account>::success(credentials->account));
        });
}

void AuthenticationService::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged(m_busy);
}

void AuthenticationService::recordFailedAttempt()
{
    ++m_consecutiveFailures;
    if (m_consecutiveFailures < AuthenticationProductPolicy::FailedAttemptsBeforeDelay) {
        return;
    }
    const int exponent = std::min(m_consecutiveFailures - AuthenticationProductPolicy::FailedAttemptsBeforeDelay, 4);
    const int delay = std::min(AuthenticationProductPolicy::InitialFailedAttemptDelayMs * (1 << exponent),
                               AuthenticationProductPolicy::MaximumFailedAttemptDelayMs);
    m_retryDurationMs = delay;
    m_retryElapsed.start();
    emit retryDelayChanged(delay);
    m_retryTimer.start();
}

domain::errors::AppError AuthenticationService::invalidCredentialsError()
{
    return domain::errors::AppError::failure(
        domain::errors::ErrorCode::AuthenticationFailed,
        domain::errors::ErrorCategory::Authentication,
        QStringLiteral("Username or password is incorrect."),
        QStringLiteral("Local account authentication was not successful."),
        QStringLiteral("authenticate local account"));
}

domain::errors::AppError AuthenticationService::busyError()
{
    return domain::errors::AppError::failure(
        domain::errors::ErrorCode::PasswordOperationBusy,
        domain::errors::ErrorCategory::Authentication,
        QStringLiteral("Wait for the current security operation to finish."),
        QStringLiteral("A second authentication operation was rejected while one was active."),
        QStringLiteral("authenticate local account"));
}

} // namespace devhub::application::authentication
