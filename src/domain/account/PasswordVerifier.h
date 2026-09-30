#pragma once

#include "../../shared/common/Result.h"

#include <QString>

namespace devhub::infrastructure::account {
class SodiumPasswordHasher;
class SqlAccountRepository;
}

namespace devhub::domain::account {

class PasswordVerifier final
{
public:
    PasswordVerifier(const PasswordVerifier&) = delete;
    PasswordVerifier& operator=(const PasswordVerifier&) = delete;
    PasswordVerifier(PasswordVerifier&&) noexcept = default;
    PasswordVerifier& operator=(PasswordVerifier&&) noexcept = default;

    [[nodiscard]] static shared::common::Result<PasswordVerifier> fromStorage(QString encoded,
                                                                               int schemeVersion);
    [[nodiscard]] int schemeVersion() const noexcept { return m_schemeVersion; }

private:
    PasswordVerifier(QString encoded, int schemeVersion);
    QString m_encoded;
    int m_schemeVersion = 0;

    friend class devhub::infrastructure::account::SodiumPasswordHasher;
    friend class devhub::infrastructure::account::SqlAccountRepository;
};

} // namespace devhub::domain::account
