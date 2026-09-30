#pragma once

#include "../../domain/account/PasswordVerifier.h"
#include "../../shared/common/Result.h"

namespace devhub::application::security {

class SecurePasswordBuffer;

class PasswordHasher
{
public:
    virtual ~PasswordHasher() = default;
    [[nodiscard]] virtual shared::common::Result<domain::account::PasswordVerifier>
    hash(const SecurePasswordBuffer& password) const = 0;
    [[nodiscard]] virtual shared::common::Result<bool>
    verify(const SecurePasswordBuffer& password,
           const domain::account::PasswordVerifier& verifier) const = 0;
    [[nodiscard]] virtual shared::common::Result<bool>
    needsRehash(const domain::account::PasswordVerifier& verifier) const = 0;
};

} // namespace devhub::application::security
