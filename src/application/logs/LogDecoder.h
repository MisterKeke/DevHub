#pragma once

#include <QByteArray>
#include <QString>
#include <QStringConverter>

namespace devhub::application::logs {

class LogDecoder final
{
public:
    [[nodiscard]] QString decode(const QByteArray& bytes);
    void reset();

private:
    QStringDecoder m_decoder{QStringConverter::Utf8};
    QByteArray m_initialBytes;
    bool m_started = false;
};

} // namespace devhub::application::logs
