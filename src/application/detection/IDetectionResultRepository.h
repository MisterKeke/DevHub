#pragma once

#include "DetectionResult.h"
#include "../../domain/workspace/WorkspaceId.h"
#include "../../shared/common/Result.h"

#include <QDateTime>
#include <QVector>

namespace devhub::application::detection {

class IDetectionResultRepository
{
public:
    virtual ~IDetectionResultRepository() = default;
    [[nodiscard]] virtual shared::common::Result<void> invalidateIfVersionChanged(
        const domain::workspace::WorkspaceId& workspaceId, const QString& detectorVersion) = 0;
    [[nodiscard]] virtual shared::common::Result<void> replaceCompletedScan(
        const domain::workspace::WorkspaceId& workspaceId, const QString& detectorVersion,
        const QVector<DetectionResult>& results, qint64 inspectedFiles,
        const QString& limitReached, const QDateTime& scannedAtUtc) = 0;
};

} // namespace devhub::application::detection
