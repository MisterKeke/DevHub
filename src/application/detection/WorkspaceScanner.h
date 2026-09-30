#pragma once

#include "WorkspaceDetectorRegistry.h"
#include "DetectionResult.h"
#include "../../shared/common/Result.h"

#include <functional>

namespace devhub::application::detection {

struct ScanReport final
{
    QString detectorVersion;
    QVector<DetectionResult> results;
    qint64 inspectedFiles = 0;
    qint64 visitedDirectories = 0;
    qint64 elapsedMilliseconds = 0;
    QString limitReached;
};

class WorkspaceScanner final
{
public:
    using CancellationCheck = std::function<bool()>;
    using ProgressReporter = std::function<void(QString, int)>;
    explicit WorkspaceScanner(const WorkspaceDetectorRegistry& registry) : m_registry(registry) {}
    [[nodiscard]] shared::common::Result<ScanReport> scan(const QString& workspaceRoot,
        const ScanBudget& budget, CancellationCheck cancelled, ProgressReporter progress) const;

private:
    WorkspaceDetectorRegistry m_registry;
};

} // namespace devhub::application::detection
