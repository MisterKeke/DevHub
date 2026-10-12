#pragma once

#include "application/assistant/AssistantTypes.h"
#include "shared/common/Result.h"
#include <QJsonObject>

namespace devhub::infrastructure::codex {
class CodexProtocolCodec final
{
public:
    explicit CodexProtocolCodec(application::assistant::CodexTransportLimits limits = {});
    [[nodiscard]] shared::common::Result<void> feed(QByteArray bytes);
    [[nodiscard]] shared::common::Result<std::optional<QJsonObject>> nextEnvelope();
    [[nodiscard]] shared::common::Result<void> finish() const;
    [[nodiscard]] shared::common::Result<QByteArray> encode(const QJsonObject& object) const;
    [[nodiscard]] bool hasRecord() const;
    void reset();
private:
    application::assistant::CodexTransportLimits m_limits;
    QByteArray m_bytes;
    qsizetype m_offset = 0;
};
} // namespace devhub::infrastructure::codex
