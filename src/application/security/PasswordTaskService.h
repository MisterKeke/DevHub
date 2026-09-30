#pragma once

#include "PasswordHasher.h"
#include "../tasks/TaskScheduler.h"

#include <QObject>

#include <functional>
#include <optional>
#include <utility>

namespace devhub::application::security {

struct PasswordVerificationResult final
{
    bool verified = false;
    std::optional<domain::account::PasswordVerifier> replacementVerifier;

    PasswordVerificationResult() = default;
    PasswordVerificationResult(bool verifiedValue,
                               std::optional<domain::account::PasswordVerifier> replacement = std::nullopt)
        : verified(verifiedValue), replacementVerifier(std::move(replacement))
    {
    }
    PasswordVerificationResult(const PasswordVerificationResult&) = delete;
    PasswordVerificationResult& operator=(const PasswordVerificationResult&) = delete;
    PasswordVerificationResult(PasswordVerificationResult&&) noexcept = default;
    PasswordVerificationResult& operator=(PasswordVerificationResult&&) noexcept = default;
};

class PasswordTaskService final : public QObject
{
    Q_OBJECT
public:
    using HashCompletion = std::function<void(shared::common::TaskOutcome<domain::account::PasswordVerifier>)>;
    using VerifyCompletion = std::function<void(shared::common::TaskOutcome<PasswordVerificationResult>)>;

    PasswordTaskService(tasks::TaskScheduler& scheduler, PasswordHasher& hasher, QObject* parent = nullptr);
    [[nodiscard]] tasks::TaskHandle hashPassword(QString passwordText, HashCompletion completion);
    [[nodiscard]] tasks::TaskHandle verifyPassword(QString passwordText,
                                                   domain::account::PasswordVerifier verifier,
                                                   VerifyCompletion completion);
    [[nodiscard]] bool isBusy() const noexcept { return m_busy; }

private:
    [[nodiscard]] static domain::errors::AppError busyError();

    tasks::TaskScheduler& m_scheduler;
    PasswordHasher& m_hasher;
    bool m_busy = false;
};

} // namespace devhub::application::security
