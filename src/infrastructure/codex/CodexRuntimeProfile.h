#pragma once
#include "shared/common/Result.h"
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QProcessEnvironment>

namespace devhub::infrastructure::codex {
class CodexRuntimeProfile final {
public:
    [[nodiscard]] static shared::common::Result<QStringList> launchArguments(QStringList disabledMcpServers = {});
    [[nodiscard]] static bool acceptsLaunch(const QStringList& arguments);
    [[nodiscard]] static QStringList rejectionReasons(const QJsonObject& configResult, const QJsonObject& requirementsResult);
    [[nodiscard]] static QString fingerprint(const QStringList& launchArguments = {});
    [[nodiscard]] static bool acceptsEnvironment(const QProcessEnvironment& environment);
    [[nodiscard]] static bool acceptsLoginUrl(const QUrl& url, bool deviceCode);
    // Phase 0 has no authenticated/native model proof. No runtime boolean can
    // turn synthetic protocol verification into live release approval.
    [[nodiscard]] static constexpr bool liveInferenceApproved() { return false; }
};
}
