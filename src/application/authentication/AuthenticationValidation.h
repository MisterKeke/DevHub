#pragma once

#include "../../domain/account/Username.h"

#include <optional>

namespace devhub::application::authentication {

struct CreateAccountInputValidation final
{
    std::optional<domain::account::DisplayUsername> displayUsername;
    std::optional<domain::account::NormalizedUsername> normalizedUsername;
    QString message;
    [[nodiscard]] bool isValid() const noexcept
    {
        return displayUsername.has_value() && normalizedUsername.has_value() && message.isEmpty();
    }
};

struct SignInInputValidation final
{
    std::optional<domain::account::NormalizedUsername> normalizedUsername;
    QString message;
    [[nodiscard]] bool isValid() const noexcept
    {
        return normalizedUsername.has_value() && message.isEmpty();
    }
};

struct UnlockInputValidation final
{
    bool valid = false;
    QString message;
};

[[nodiscard]] CreateAccountInputValidation validateCreateAccountInput(
    const QString& username, const QString& password, const QString& confirmation);
[[nodiscard]] SignInInputValidation validateSignInInput(const QString& username,
                                                        const QString& password);
[[nodiscard]] UnlockInputValidation validateUnlockInput(const QString& password);

} // namespace devhub::application::authentication
