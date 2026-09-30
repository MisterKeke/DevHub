#pragma once

#include "../../application/security/PasswordHasher.h"

#include <cstddef>

namespace devhub::infrastructure::account {

struct PasswordHashPolicy final
{
    unsigned long long operationsLimit = 0;
    std::size_t memoryLimitBytes = 0;
    int schemeVersion = 1;

    [[nodiscard]] static PasswordHashPolicy production();
    [[nodiscard]] static PasswordHashPolicy fastTest();
};

class SodiumPasswordHasher final : public application::security::PasswordHasher
{
public:
    explicit SodiumPasswordHasher(PasswordHashPolicy policy = PasswordHashPolicy::production());

    [[nodiscard]] static shared::common::Result<void> initialize();
    [[nodiscard]] shared::common::Result<domain::account::PasswordVerifier>
    hash(const application::security::SecurePasswordBuffer& password) const override;
    [[nodiscard]] shared::common::Result<bool>
    verify(const application::security::SecurePasswordBuffer& password,
           const domain::account::PasswordVerifier& verifier) const override;
    [[nodiscard]] shared::common::Result<bool>
    needsRehash(const domain::account::PasswordVerifier& verifier) const override;
    [[nodiscard]] const PasswordHashPolicy& policy() const noexcept { return m_policy; }

private:
    [[nodiscard]] shared::common::Result<void> ensureWorkerThread(const QString& operation) const;
    PasswordHashPolicy m_policy;
};

} // namespace devhub::infrastructure::account
