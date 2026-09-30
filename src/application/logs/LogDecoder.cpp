#include "LogDecoder.h"

namespace devhub::application::logs {

QString LogDecoder::decode(const QByteArray& bytes)
{
    if (bytes.isEmpty()) return {};
    if (!m_started) {
        m_initialBytes.append(bytes);
        if (m_initialBytes.isEmpty()) return {};
        if (static_cast<unsigned char>(m_initialBytes.at(0)) == 0xEF) {
            if (m_initialBytes.size() < 3) return {};
            if (m_initialBytes.startsWith("\xEF\xBB\xBF")) m_initialBytes.remove(0, 3);
        }
        m_started = true;
        return m_decoder(QByteArrayView(m_initialBytes));
    }
    return m_decoder(QByteArrayView(bytes));
}

void LogDecoder::reset()
{
    m_decoder = QStringDecoder(QStringConverter::Utf8);
    m_initialBytes.clear();
    m_started = false;
}

} // namespace devhub::application::logs
