#include "CodexWriteQueue.h"

namespace devhub::infrastructure::codex {
using shared::common::Result;
namespace {
domain::errors::AppError writeError(domain::errors::ErrorCode code, const QString& message)
{
    return domain::errors::AppError::failure(code, domain::errors::ErrorCategory::Process,
        message, {}, QStringLiteral("codex.transport"));
}
}
Result<void> CodexWriteQueue::enqueue(const QByteArray& bytes, qint64 inFlightBytes)
{
    if (inFlightBytes < 0 || inFlightBytes > m_limitBytes
        || size() > m_limitBytes - inFlightBytes
        || bytes.size() > m_limitBytes - inFlightBytes - size())
        return Result<void>::failure(writeError(domain::errors::ErrorCode::ConcurrencyLimitReached,
            QStringLiteral("The Assistant write queue is full.")));
    m_bytes.append(bytes);
    return Result<void>::success();
}
Result<qint64> CodexWriteQueue::flush(const Writer& writer)
{
    if (m_bytes.isEmpty()) return Result<qint64>::success(0);
    const auto accepted = writer(m_bytes.constData(), size());
    if (accepted < 0 || accepted > size())
        return Result<qint64>::failure(writeError(domain::errors::ErrorCode::IoFailure,
            QStringLiteral("The Assistant input pipe failed.")));
    // A short/zero write preserves the exact unaccepted suffix, including UTF-8
    // fragments and record delimiters. Remote receipt is not implied.
    m_bytes.remove(0, static_cast<qsizetype>(accepted));
    return Result<qint64>::success(accepted);
}
} // namespace devhub::infrastructure::codex
