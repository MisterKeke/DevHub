#pragma once

#include "BuildTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::buildtest {

class BuildSystemDetector final
{
public:
    [[nodiscard]] shared::common::Result<DetectedProject> detect(const QString& workspaceId,
                                                                 const QString& workspaceRoot) const;
    [[nodiscard]] QVector<BuildProfile> defaultProfiles(const DetectedProject& project) const;
};

} // namespace devhub::application::buildtest
