#pragma once

#include "IWorkspaceDetector.h"

#include <memory>
#include <QVector>

namespace devhub::application::detection {

class WorkspaceDetectorRegistry final
{
public:
    WorkspaceDetectorRegistry();
    explicit WorkspaceDetectorRegistry(QVector<std::shared_ptr<const IWorkspaceDetector>> detectors);
    void registerDetector(std::shared_ptr<const IWorkspaceDetector> detector);
    [[nodiscard]] QVector<DetectionEvidence> inspect(const DetectionEntry& entry,
                                                     const ScanBudget& budget) const;
    [[nodiscard]] QString outputVersion() const;
    [[nodiscard]] const QVector<std::shared_ptr<const IWorkspaceDetector>>& detectors() const noexcept
    { return m_detectors; }

private:
    QVector<std::shared_ptr<const IWorkspaceDetector>> m_detectors;
};

} // namespace devhub::application::detection
