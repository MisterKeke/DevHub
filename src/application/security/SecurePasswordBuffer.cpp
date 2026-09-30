#include "SecurePasswordBuffer.h"

#include <sodium.h>

#include <cstddef>

namespace devhub::application::security {

SecurePasswordBuffer::SecurePasswordBuffer(QString passwordText)
    : m_utf8Password(passwordText.toUtf8())
{
    if (!passwordText.isEmpty()) {
        const auto byteCount = static_cast<std::size_t>(passwordText.size()) * sizeof(QChar);
        sodium_memzero(passwordText.data(), byteCount);
    }
    passwordText.clear();
}

SecurePasswordBuffer::~SecurePasswordBuffer()
{
    clear();
}

void SecurePasswordBuffer::clear() noexcept
{
    if (!m_utf8Password.isEmpty()) {
        sodium_memzero(m_utf8Password.data(), static_cast<std::size_t>(m_utf8Password.size()));
        m_utf8Password.clear();
    }
}

} // namespace devhub::application::security
