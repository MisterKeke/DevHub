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
    [[nodiscard]] static QString fingerprint(const QString& version, const QStringList& launchArguments);
    [[nodiscard]] static bool acceptsEnvironment(const QProcessEnvironment& environment);
    [[nodiscard]] static bool acceptsLoginUrl(const QUrl& url, bool deviceCode);
    // Production chat is enabled for the pinned alpha requested by the user.
    // Each launch must still verify its effective profile and ChatGPT account.
    // Dedicated live-test builds retain their explicit disposable-home opt-in.
    [[nodiscard]] static bool productionChatEnabled(const QString& version);
    // True only in the separate static library linked by the explicit live-test
    // executable. This is not a production approval and is not user-configurable.
    [[nodiscard]] static bool developmentLiveValidationBuild();
    // Per-launch validation authorization. A live-test build must still provide
    // explicit acknowledgements, the exact supported executable and an isolated,
    // sentinel-marked CODEX_HOME. Production builds always return false.
    [[nodiscard]] static bool developmentLiveInferenceAuthorized(
        const QString& executablePath, const QString& version, const QProcessEnvironment& childEnvironment);
};
}
