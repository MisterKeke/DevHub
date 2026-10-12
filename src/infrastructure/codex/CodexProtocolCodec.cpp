#include "CodexProtocolCodec.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringDecoder>
#include <cmath>

namespace devhub::infrastructure::codex {
using shared::common::Result;
namespace {
domain::errors::AppError protocolError(const QString& detail)
{
    return domain::errors::AppError::failure(domain::errors::ErrorCode::IoFailure,
        domain::errors::ErrorCategory::Process, QStringLiteral("The Assistant protocol failed."),
        detail, QStringLiteral("codex.protocol"));
}
bool validId(const QJsonValue& id)
{
    if (id.isString()) return true;
    if (!id.isDouble()) return false;
    const double number = id.toDouble();
    return std::isfinite(number) && std::floor(number) == number && std::abs(number) <= 9007199254740991.0;
}
bool validEnvelope(const QJsonObject& object)
{
    const bool id = object.contains(QStringLiteral("id"));
    const bool method = object.contains(QStringLiteral("method"));
    const bool result = object.contains(QStringLiteral("result"));
    const bool error = object.contains(QStringLiteral("error"));
    if (id && !validId(object.value(QStringLiteral("id")))) return false;
    if (method) {
        if (!object.value(QStringLiteral("method")).isString()
            || object.value(QStringLiteral("method")).toString().isEmpty() || result || error) return false;
        const auto params = object.value(QStringLiteral("params"));
        return params.isUndefined() || params.isNull() || params.isObject() || params.isArray();
    }
    if (!id || result == error || object.contains(QStringLiteral("params"))) return false;
    if (error) {
        const auto value = object.value(QStringLiteral("error"));
        const auto code = value.toObject().value(QStringLiteral("code"));
        return value.isObject() && validId(code) && code.isDouble()
            && value.toObject().value(QStringLiteral("message")).isString();
    }
    return true;
}
}
CodexProtocolCodec::CodexProtocolCodec(application::assistant::CodexTransportLimits limits)
    : m_limits(limits) {}

Result<void> CodexProtocolCodec::feed(QByteArray bytes)
{
    if (bytes.size() > m_limits.framingBytes - (m_bytes.size() - m_offset))
        return Result<void>::failure(protocolError(QStringLiteral("Framing buffer limit exceeded.")));
    if (m_offset) { m_bytes.remove(0, m_offset); m_offset = 0; }
    m_bytes.append(bytes);
    // Check the trailing fragment as well as complete records in nextEnvelope.
    const auto lastLf = m_bytes.lastIndexOf('\n');
    if (m_bytes.size() - lastLf - 1 > m_limits.recordBytes + 1)
        return Result<void>::failure(protocolError(QStringLiteral("Record limit exceeded.")));
    return Result<void>::success();
}
bool CodexProtocolCodec::hasRecord() const { return m_bytes.indexOf('\n', m_offset) >= 0; }
Result<std::optional<QJsonObject>> CodexProtocolCodec::nextEnvelope()
{
    const auto lf = m_bytes.indexOf('\n', m_offset);
    if (lf < 0) return Result<std::optional<QJsonObject>>::success(std::nullopt);
    auto size = lf - m_offset;
    if (size && m_bytes.at(lf - 1) == '\r') --size;
    if (size > m_limits.recordBytes)
        return Result<std::optional<QJsonObject>>::failure(protocolError(QStringLiteral("Record limit exceeded.")));
    const auto line = m_bytes.mid(m_offset, size);
    m_offset = lf + 1;
    if (line.isEmpty()) return Result<std::optional<QJsonObject>>::success(std::nullopt);
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder.decode(line);
    Q_UNUSED(decoded);
    if (decoder.hasError())
        return Result<std::optional<QJsonObject>>::failure(protocolError(QStringLiteral("Invalid UTF-8 record.")));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !validEnvelope(document.object()))
        return Result<std::optional<QJsonObject>>::failure(protocolError(QStringLiteral("Invalid JSON object envelope.")));
    return Result<std::optional<QJsonObject>>::success(document.object());
}
Result<void> CodexProtocolCodec::finish() const
{
    if (m_bytes.size() != m_offset)
        return Result<void>::failure(protocolError(QStringLiteral("Truncated protocol record at EOF.")));
    return Result<void>::success();
}
Result<QByteArray> CodexProtocolCodec::encode(const QJsonObject& object) const
{
    if (!validEnvelope(object)) return Result<QByteArray>::failure(protocolError(QStringLiteral("Invalid outgoing envelope.")));
    auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > m_limits.recordBytes)
        return Result<QByteArray>::failure(protocolError(QStringLiteral("Outgoing record limit exceeded.")));
    bytes.append('\n');
    return Result<QByteArray>::success(std::move(bytes));
}
void CodexProtocolCodec::reset() { m_bytes.clear(); m_offset = 0; }
} // namespace devhub::infrastructure::codex
