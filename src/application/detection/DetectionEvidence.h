#pragma once
#include "DetectorId.h"
#include <QString>
#include <utility>

namespace devhub::application::detection {
enum class ConfidenceLevel { Low = 0, Medium = 1, High = 2 };

struct DetectionEvidence final
{
    DetectorId detectorId;
    QString relativePath;
    QString relativeDirectory;
    QString description;
    ConfidenceLevel confidence = ConfidenceLevel::Low;
    int detectorVersion = 1;
};
}
