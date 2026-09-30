#include "SodiumPasswordHasher.h"

#include "../../application/security/SecurePasswordBuffer.h"

#include <QCoreApplication>
#include <QThread>

#include <sodium.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace devhub::infrastructure::account {
namespace {

std::atomic_bool sodiumInitialized{false};

shared::common::Result<void> passwordError(domain::errors::ErrorCode code,
                                           const QString& userMessage,
                                           const QString& details,
                                           const QString& operation)
{
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        code, domain::errors::ErrorCategory::Security, userMessage, details, operation));
}

template<typename T>
shared::common::Result<T> passwordResultError(domain::errors::ErrorCode code,
                                              const QString& userMessage,
                                              const QString& details,
                                              const QString& operation)
{
    return shared::common::Result<T>::failure(domain::errors::AppError::failure(
        code, domain::errors::ErrorCategory::Security, userMessage, details, operation));
}

shared::common::Result<void> invalidVerifierError(const QString& operation)
{
    return passwordError(domain::errors::ErrorCode::InvalidPasswordHash,
                         QStringLiteral("The local account data is invalid."),
                         QStringLiteral("Stored password verifier could not be parsed by libsodium."),
                         operation);
}

} // namespace

PasswordHashPolicy PasswordHashPolicy::production()
{
    return {crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE,
            1};
}

PasswordHashPolicy PasswordHashPolicy::fastTest()
{
    return {1, 8U * 1024U * 1024U, 1};
}

SodiumPasswordHasher::SodiumPasswordHasher(PasswordHashPolicy policy)
    : m_policy(policy)
{
}

shared::common::Result<void> SodiumPasswordHasher::initialize()
{
    const int result = sodium_init();
    if (result == -1) {
        return passwordError(domain::errors::ErrorCode::PasswordHashFailed,
                             QStringLiteral("DevHub could not initialize its security provider."),
                             QStringLiteral("libsodium sodium_init() failed."),
                             QStringLiteral("initialize password security"));
    }
    sodiumInitialized.store(true, std::memory_order_release);
    return shared::common::Result<void>::success();
}

shared::common::Result<void> SodiumPasswordHasher::ensureWorkerThread(const QString& operation) const
{
    if (!sodiumInitialized.load(std::memory_order_acquire)) {
        return passwordError(domain::errors::ErrorCode::PasswordHashFailed,
                             QStringLiteral("DevHub security services are not ready."),
                             QStringLiteral("libsodium has not been initialized."),
                             operation);
    }
    const QCoreApplication* application = QCoreApplication::instance();
    if (application != nullptr && QThread::currentThread() == application->thread()) {
        return passwordError(domain::errors::ErrorCode::PasswordHashFailed,
                             QStringLiteral("Password work must run in the background."),
                             QStringLiteral("Password hashing API was called from the application thread."),
                             operation);
    }
    if (m_policy.operationsLimit < crypto_pwhash_OPSLIMIT_MIN
        || m_policy.operationsLimit > crypto_pwhash_OPSLIMIT_MAX
        || m_policy.memoryLimitBytes < crypto_pwhash_MEMLIMIT_MIN
        || m_policy.memoryLimitBytes > crypto_pwhash_MEMLIMIT_MAX
        || m_policy.schemeVersion < 1) {
        return passwordError(domain::errors::ErrorCode::PasswordHashFailed,
                             QStringLiteral("DevHub could not apply its password security policy."),
                             QStringLiteral("Configured Argon2id parameters are outside libsodium limits."),
                             operation);
    }
    return shared::common::Result<void>::success();
}

shared::common::Result<domain::account::PasswordVerifier>
SodiumPasswordHasher::hash(const application::security::SecurePasswordBuffer& password) const
{
    auto threadResult = ensureWorkerThread(QStringLiteral("hash account password"));
    if (!threadResult) {
        return shared::common::Result<domain::account::PasswordVerifier>::failure(threadResult.error());
    }
    if (static_cast<unsigned long long>(password.size()) > crypto_pwhash_PASSWD_MAX) {
        return passwordResultError<domain::account::PasswordVerifier>(
            domain::errors::ErrorCode::InvalidArgument,
            QStringLiteral("The password is too long for the configured security provider."),
            QStringLiteral("Password byte length exceeds libsodium’s maximum input length."),
            QStringLiteral("hash account password"));
    }

    std::array<char, crypto_pwhash_STRBYTES> encoded{};
    const int result = crypto_pwhash_str_alg(encoded.data(),
                                             password.m_utf8Password.constData(),
                                             static_cast<unsigned long long>(password.size()),
                                             m_policy.operationsLimit,
                                             m_policy.memoryLimitBytes,
                                             crypto_pwhash_ALG_ARGON2ID13);
    if (result != 0) {
        sodium_memzero(encoded.data(), encoded.size());
        return passwordResultError<domain::account::PasswordVerifier>(
            domain::errors::ErrorCode::PasswordHashFailed,
            QStringLiteral("DevHub could not protect the account password."),
            QStringLiteral("libsodium Argon2id password hashing did not complete."),
            QStringLiteral("hash account password"));
    }
    QString encodedText = QString::fromLatin1(encoded.data());
    sodium_memzero(encoded.data(), encoded.size());
    return domain::account::PasswordVerifier::fromStorage(std::move(encodedText), m_policy.schemeVersion);
}

shared::common::Result<bool>
SodiumPasswordHasher::verify(const application::security::SecurePasswordBuffer& password,
                             const domain::account::PasswordVerifier& verifier) const
{
    auto threadResult = ensureWorkerThread(QStringLiteral("verify account password"));
    if (!threadResult) {
        return shared::common::Result<bool>::failure(threadResult.error());
    }
    QByteArray encoded = verifier.m_encoded.toLatin1();
    if (crypto_pwhash_str_needs_rehash(encoded.constData(),
                                       m_policy.operationsLimit,
                                       m_policy.memoryLimitBytes) == -1) {
        sodium_memzero(encoded.data(), static_cast<std::size_t>(encoded.size()));
        return shared::common::Result<bool>::failure(invalidVerifierError(QStringLiteral("verify account password")).error());
    }
    const int verification = crypto_pwhash_str_verify(encoded.constData(),
                                                      password.m_utf8Password.constData(),
                                                      static_cast<unsigned long long>(password.size()));
    sodium_memzero(encoded.data(), static_cast<std::size_t>(encoded.size()));
    return shared::common::Result<bool>::success(verification == 0);
}

shared::common::Result<bool>
SodiumPasswordHasher::needsRehash(const domain::account::PasswordVerifier& verifier) const
{
    auto threadResult = ensureWorkerThread(QStringLiteral("check password hash policy"));
    if (!threadResult) {
        return shared::common::Result<bool>::failure(threadResult.error());
    }
    if (verifier.schemeVersion() != m_policy.schemeVersion) {
        return shared::common::Result<bool>::success(true);
    }
    QByteArray encoded = verifier.m_encoded.toLatin1();
    const int result = crypto_pwhash_str_needs_rehash(encoded.constData(),
                                                     m_policy.operationsLimit,
                                                     m_policy.memoryLimitBytes);
    sodium_memzero(encoded.data(), static_cast<std::size_t>(encoded.size()));
    if (result == -1) {
        return shared::common::Result<bool>::failure(invalidVerifierError(QStringLiteral("check password hash policy")).error());
    }
    return shared::common::Result<bool>::success(result == 1);
}

} // namespace devhub::infrastructure::account
