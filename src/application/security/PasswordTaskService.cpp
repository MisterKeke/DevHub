#include "PasswordTaskService.h"

#include "SecurePasswordBuffer.h"

#include <QPointer>
#include <QThread>
#include <QTimer>

#include <memory>
#include <optional>
#include <utility>

namespace devhub::application::security {

PasswordTaskService::PasswordTaskService(tasks::TaskScheduler& scheduler,
                                         PasswordHasher& hasher,
                                         QObject* parent)
    : QObject(parent), m_scheduler(scheduler), m_hasher(hasher)
{
}

domain::errors::AppError PasswordTaskService::busyError()
{
    return domain::errors::AppError::failure(
        domain::errors::ErrorCode::PasswordOperationBusy,
        domain::errors::ErrorCategory::Security,
        QStringLiteral("Password security work is already in progress."),
        QStringLiteral("A duplicate password hash or verification request was rejected."),
        QStringLiteral("schedule password operation"));
}

tasks::TaskHandle PasswordTaskService::hashPassword(QString passwordText, HashCompletion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_busy) {
        QTimer::singleShot(0, this, [completion = std::move(completion)]() mutable {
            completion(shared::common::TaskOutcome<domain::account::PasswordVerifier>{
                shared::common::Result<domain::account::PasswordVerifier>::failure(busyError())});
        });
        return {QUuid(), tasks::CancellationSource()};
    }

    m_busy = true;
    auto password = std::make_shared<SecurePasswordBuffer>(std::move(passwordText));
    QPointer<PasswordTaskService> service(this);
    PasswordHasher* hasher = &m_hasher;
    tasks::TaskRequest request{QStringLiteral("account-password"), std::nullopt, 0, {}};
    return m_scheduler.submit<domain::account::PasswordVerifier>(
        std::move(request),
        [password, hasher](const tasks::TaskContext&) {
            auto result = hasher->hash(*password);
            password->clear();
            return result;
        },
        [service, completion = std::move(completion)](
            shared::common::TaskOutcome<domain::account::PasswordVerifier> outcome) mutable {
            if (service) {
                service->m_busy = false;
                completion(std::move(outcome));
            }
        },
        10);
}

tasks::TaskHandle PasswordTaskService::verifyPassword(QString passwordText,
                                                      domain::account::PasswordVerifier verifier,
                                                      VerifyCompletion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_busy) {
        QTimer::singleShot(0, this, [completion = std::move(completion)]() mutable {
            completion(shared::common::TaskOutcome<PasswordVerificationResult>{
                shared::common::Result<PasswordVerificationResult>::failure(busyError())});
        });
        return {QUuid(), tasks::CancellationSource()};
    }

    m_busy = true;
    auto password = std::make_shared<SecurePasswordBuffer>(std::move(passwordText));
    auto storedVerifier = std::make_shared<domain::account::PasswordVerifier>(std::move(verifier));
    QPointer<PasswordTaskService> service(this);
    PasswordHasher* hasher = &m_hasher;
    tasks::TaskRequest request{QStringLiteral("account-password"), std::nullopt, 0, {}};
    return m_scheduler.submit<PasswordVerificationResult>(
        std::move(request),
        [password, storedVerifier, hasher](const tasks::TaskContext&) {
            auto verified = hasher->verify(*password, *storedVerifier);
            if (!verified) {
                password->clear();
                return shared::common::Result<PasswordVerificationResult>::failure(verified.error());
            }
            if (!verified.value()) {
                password->clear();
                return shared::common::Result<PasswordVerificationResult>::success(
                    PasswordVerificationResult(false));
            }
            auto rehashRequired = hasher->needsRehash(*storedVerifier);
            if (!rehashRequired) {
                password->clear();
                return shared::common::Result<PasswordVerificationResult>::failure(rehashRequired.error());
            }
            std::optional<domain::account::PasswordVerifier> replacementVerifier;
            if (rehashRequired.value()) {
                auto replacement = hasher->hash(*password);
                if (!replacement) {
                    password->clear();
                    return shared::common::Result<PasswordVerificationResult>::failure(replacement.error());
                }
                replacementVerifier.emplace(std::move(replacement).value());
            }
            password->clear();
            return shared::common::Result<PasswordVerificationResult>::success(
                PasswordVerificationResult(true, std::move(replacementVerifier)));
        },
        [service, completion = std::move(completion)](
            shared::common::TaskOutcome<PasswordVerificationResult> outcome) mutable {
            if (service) {
                service->m_busy = false;
                completion(std::move(outcome));
            }
        },
        10);
}

} // namespace devhub::application::security
