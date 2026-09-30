#pragma once

#include "DetectionEvidence.h"
#include "ScanBudget.h"

#include <QFileInfo>

namespace devhub::application::detection {

struct DetectionEntry final
{
    QFileInfo info;
    QString name;
    QString relativePath;
    QString relativeDirectory;
    bool isDirectory = false;
    qint64 size = 0;
};

class IWorkspaceDetector
{
public:
    virtual ~IWorkspaceDetector() = default;
    [[nodiscard]] virtual DetectorId id() const = 0;
    [[nodiscard]] virtual int priority() const noexcept = 0;
    [[nodiscard]] virtual int version() const noexcept = 0;
    [[nodiscard]] virtual QVector<DetectionEvidence> inspect(const DetectionEntry& entry,
                                                              const ScanBudget& budget) const = 0;
};

} // namespace devhub::application::detection
