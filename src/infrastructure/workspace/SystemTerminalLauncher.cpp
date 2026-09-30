#include "SystemTerminalLauncher.h"
#include <QDir>
#include <QProcess>

namespace devhub::infrastructure::workspace {
shared::common::Result<void> SystemTerminalLauncher::openAt(const QString& directory)
{
#ifdef Q_OS_WIN
    const QString program = QStringLiteral("powershell.exe");
    QString quotedDirectory = directory;
    quotedDirectory.replace(QLatin1Char('\''), QStringLiteral("''"));
    const QStringList arguments{QStringLiteral("-NoExit"), QStringLiteral("-Command"),
                                QStringLiteral("Set-Location -LiteralPath '%1'").arg(quotedDirectory)};
#elif defined(Q_OS_MACOS)
    const QString program = QStringLiteral("open");
    const QStringList arguments{QStringLiteral("-a"), QStringLiteral("Terminal"), directory};
#else
    const QString program = QStringLiteral("x-terminal-emulator");
    const QStringList arguments{QStringLiteral("--working-directory=%1").arg(directory)};
#endif
    if (!QProcess::startDetached(program, arguments, directory))
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Process,
            QStringLiteral("DevHub could not open a terminal in this workspace."),
            QStringLiteral("Unable to launch %1 in %2").arg(program, directory),
            QStringLiteral("open workspace terminal")));
    return shared::common::Result<void>::success();
}
}
