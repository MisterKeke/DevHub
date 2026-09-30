#include "GitExecutableLocator.h"
#include <QProcess>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>

namespace devhub::application::git {
namespace {
domain::errors::AppError gitError(QString user, QString technical) {
    return domain::errors::AppError::failure(domain::errors::ErrorCode::InternalFailure,
        domain::errors::ErrorCategory::Git, std::move(user), std::move(technical), QStringLiteral("locate Git"));
}
}
shared::common::Result<GitExecutable> GitExecutableLocator::validate(const QString& path) const {
    if (path.trimmed().isEmpty()) return shared::common::Result<GitExecutable>::failure(
        gitError(QStringLiteral("Choose a Git executable."), QStringLiteral("Git executable path is empty.")));
    QProcess process;
    process.setProgram(path);
    process.setArguments({QStringLiteral("--version")});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(3000) || !process.waitForFinished(3000)) {
        process.kill(); process.waitForFinished();
        return shared::common::Result<GitExecutable>::failure(gitError(
            QStringLiteral("Git could not be started. Check the executable path."), process.errorString()));
    }
    const QString versionText = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    static const QRegularExpression pattern(QStringLiteral("^git version (\\d+)\\.(\\d+)\\.(\\d+)(?:\\.|$)"));
    const auto match = pattern.match(versionText);
    if (process.exitCode() != 0 || !match.hasMatch())
        return shared::common::Result<GitExecutable>::failure(gitError(
            QStringLiteral("The selected file did not report a valid Git version."), versionText));
    GitVersion version{match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt(), versionText};
    if (!version.supported()) return shared::common::Result<GitExecutable>::failure(gitError(
        QStringLiteral("DevHub requires Git 2.40 or newer."), versionText));
    return shared::common::Result<GitExecutable>::success({QFileInfo(path).absoluteFilePath(), version});
}
shared::common::Result<GitExecutable> GitExecutableLocator::locate() const {
    const QString configured = QSettings().value(QStringLiteral("git/executablePath")).toString();
    if (!configured.isEmpty()) {
        auto result = validate(configured);
        if (result) return result;
    }
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (fromPath.isEmpty()) return shared::common::Result<GitExecutable>::failure(gitError(
        QStringLiteral("Git was not found. Install Git or choose its executable in Settings."),
        QStringLiteral("No configured Git executable or Git executable on PATH.")));
    return validate(fromPath);
}
void GitExecutableLocator::store(const GitExecutable& executable) const {
    QSettings().setValue(QStringLiteral("git/executablePath"), executable.path);
}
}
