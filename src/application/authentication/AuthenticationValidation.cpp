#include "AuthenticationValidation.h"

namespace devhub::application::authentication {

CreateAccountInputValidation validateCreateAccountInput(const QString& username,
                                                        const QString& password,
                                                        const QString& confirmation)
{
    CreateAccountInputValidation validation;
    if (username.trimmed().isEmpty()) {
        validation.message = QStringLiteral("Enter a username.");
        return validation;
    }
    auto display = domain::account::DisplayUsername::create(username);
    if (!display) {
        validation.message = display.error().userMessage;
        return validation;
    }
    auto normalized = domain::account::NormalizedUsername::fromDisplay(display.value());
    if (!normalized) {
        validation.message = normalized.error().userMessage;
        return validation;
    }
    if (password.isEmpty()) {
        validation.message = QStringLiteral("Enter a password.");
        return validation;
    }
    if (confirmation.isEmpty()) {
        validation.message = QStringLiteral("Confirm your password.");
        return validation;
    }
    if (password != confirmation) {
        validation.message = QStringLiteral("The passwords do not match.");
        return validation;
    }
    validation.displayUsername.emplace(std::move(display).value());
    validation.normalizedUsername.emplace(std::move(normalized).value());
    return validation;
}

SignInInputValidation validateSignInInput(const QString& username, const QString& password)
{
    SignInInputValidation validation;
    if (username.trimmed().isEmpty() || password.isEmpty()) {
        validation.message = QStringLiteral("Enter your username and password.");
        return validation;
    }
    auto normalized = domain::account::NormalizedUsername::create(username);
    if (!normalized) {
        validation.message = QStringLiteral("Username or password is incorrect.");
        return validation;
    }
    validation.normalizedUsername.emplace(std::move(normalized).value());
    return validation;
}

UnlockInputValidation validateUnlockInput(const QString& password)
{
    if (password.isEmpty()) {
        return {false, QStringLiteral("Enter your password.")};
    }
    return {true, {}};
}

} // namespace devhub::application::authentication
