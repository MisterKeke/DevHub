#include "PasswordVerifier.h"

#include <utility>

namespace devhub::domain::account {

PasswordVerifier::PasswordVerifier(QString encoded, int schemeVersion)
    : m_encoded(std::move(encoded)), m_schemeVersion(schemeVersion)
{
}

shared::common::Result<PasswordVerifier> PasswordVerifier::fromStorage(QString encoded, int schemeVersion)
{
    constexpr qsizetype maximumEncodedSize = 128;
    bool asciiOnly = true;
    for (const QChar character : encoded) {
        asciiOnly = asciiOnly && character.unicode() <= 0x7f && character.unicode() != 0;
    }
    if (encoded.isEmpty() || encoded.size() >= maximumEncodedSize || schemeVersion < 1 || !asciiOnly) {
        return shared::common::Result<PasswordVerifier>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidPasswordHash,
            domain::errors::ErrorCategory::Security,
            QStringLiteral("The local account data is invalid."),
            QStringLiteral("Stored password verifier has an invalid length or scheme version."),
            QStringLiteral("load password verifier")));
    }
    return shared::common::Result<PasswordVerifier>::success(
        PasswordVerifier(std::move(encoded), schemeVersion));
}

} // namespace devhub::domain::account
