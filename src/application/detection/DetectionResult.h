#pragma once
#include "DetectionEvidence.h"
#include <QVector>

namespace devhub::application::detection {
struct DetectionResult final
{
    QString relativeDirectory;
    QVector<DetectionEvidence> evidence;
    ConfidenceLevel confidence = ConfidenceLevel::Low;
};
}
