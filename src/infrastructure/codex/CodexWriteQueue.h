#pragma once

#include "shared/common/Result.h"
#include <QByteArray>
#include <functional>

namespace devhub::infrastructure::codex {
// Internal byte queue, shared by QProcess transport and deterministic pipe-write
// tests. The writer is used only during flush, and returns accepted bytes or -1.
class CodexWriteQueue final
{
public:
    using Writer = std::function<qint64(const char*, qint64)>;
    explicit CodexWriteQueue(qint64 limitBytes) : m_limitBytes(limitBytes) {}
    [[nodiscard]] shared::common::Result<void> enqueue(const QByteArray& bytes, qint64 inFlightBytes);
    [[nodiscard]] shared::common::Result<qint64> flush(const Writer& writer);
    [[nodiscard]] qint64 size() const { return m_bytes.size(); }
    void clear() { m_bytes.clear(); }
private:
    const qint64 m_limitBytes;
    QByteArray m_bytes;
};
} // namespace devhub::infrastructure::codex
