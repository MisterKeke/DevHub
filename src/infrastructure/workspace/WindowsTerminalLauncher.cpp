#include "WindowsTerminalLauncher.h"
#include "../../application/settings/SettingsService.h"
#include "../../domain/errors/AppError.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace devhub::infrastructure::workspace {
namespace {
shared::common::Result<void> launchFailure(QString detail)
{
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Process,
        QStringLiteral("DevHub could not open the selected terminal profile."), std::move(detail),
        QStringLiteral("launch workspace terminal")));
}
}

shared::common::Result<void> WindowsTerminalLauncher::openAt(const QString& directory)
{
    const QString path = QDir(directory).absolutePath();
    if (!QFileInfo(path).isDir()) return launchFailure(QStringLiteral("The workspace directory does not exist."));
    const QString profile = m_settings.value(QStringLiteral("terminal/profile")).toString();
    QString program;
    QStringList arguments;
    if (profile == QStringLiteral("windows-terminal")) {
        program = QStringLiteral("wt.exe");
        arguments = {QStringLiteral("-d"), path};
    } else if (profile == QStringLiteral("powershell")) {
        program = QStringLiteral("powershell.exe");
        QString escaped = path; escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
        arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit"), QStringLiteral("-Command"),
                     QStringLiteral("Set-Location -LiteralPath '%1'").arg(escaped)};
    } else if (profile == QStringLiteral("cmd")) {
        QProcess process;
        process.setProgram(QStringLiteral("cmd.exe"));
        process.setWorkingDirectory(path);
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NEW_CONSOLE;
            args->startupInfo->dwFlags &= ~static_cast<DWORD>(STARTF_USESTDHANDLES);
        });
#endif
        if (!process.startDetached()) return launchFailure(process.errorString());
        return shared::common::Result<void>::success();
    } else if (profile == QStringLiteral("wsl")) {
        program = QStringLiteral("wsl.exe");
        arguments = {QStringLiteral("--cd"), path};
    } else if (profile == QStringLiteral("custom")) {
        program = m_settings.value(QStringLiteral("terminal/customExecutable")).toString().trimmed();
        const QString templateText = m_settings.value(QStringLiteral("terminal/customArguments")).toString();
        static const QRegularExpression unknownPlaceholder(QStringLiteral("\\{([^}]+)\\}"));
        auto match = unknownPlaceholder.globalMatch(templateText);
        while (match.hasNext()) {
            const auto found = match.next();
            if (found.captured(1) != QStringLiteral("workspace") && found.captured(1) != QStringLiteral("workspaceName"))
                return launchFailure(QStringLiteral("Unsupported custom terminal argument placeholder."));
        }
        arguments = QProcess::splitCommand(templateText);
        for (QString& argument : arguments) {
            argument.replace(QStringLiteral("{workspace}"), path);
            argument.replace(QStringLiteral("{workspaceName}"), QFileInfo(path).fileName());
        }
    } else {
        return launchFailure(QStringLiteral("The configured terminal profile is not supported."));
    }
    if (program.isEmpty()) return launchFailure(QStringLiteral("Choose a terminal executable in Settings."));
    if (!QProcess::startDetached(program, arguments, path))
        return launchFailure(QStringLiteral("Unable to start %1 in the workspace directory.").arg(program));
    return shared::common::Result<void>::success();
}
} // namespace devhub::infrastructure::workspace
