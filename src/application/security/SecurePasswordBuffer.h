#pragma once

#include <QByteArray>
#include <QString>

namespace devhub::infrastructure::account {
class SodiumPasswordHasher;
}

namespace devhub::application::security {

class SecurePasswordBuffer final
{
public:
    explicit SecurePasswordBuffer(QString passwordText);
    ~SecurePasswordBuffer();
    SecurePasswordBuffer(const SecurePasswordBuffer&) = delete;
    SecurePasswordBuffer& operator=(const SecurePasswordBuffer&) = delete;
    SecurePasswordBuffer(SecurePasswordBuffer&&) = delete;
    SecurePasswordBuffer& operator=(SecurePasswordBuffer&&) = delete;

    void clear() noexcept;
    [[nodiscard]] qsizetype size() const noexcept { return m_utf8Password.size(); }

private:
    QByteArray m_utf8Password;

    friend class devhub::infrastructure::account::SodiumPasswordHasher;
};

} // namespace devhub::application::security
