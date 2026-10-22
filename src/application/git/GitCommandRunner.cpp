#include "GitCommandRunner.h"
#include "../../shared/security/RedactionService.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

namespace devhub::application::git {
GitCommandResult GitCommandRunner::run(const GitCommand& command, const tasks::CancellationToken& cancellation) const {
    GitCommandResult result;
    QElapsedTimer timer; timer.start();
    QProcess process;
    process.setProgram(m_executablePath);
    QStringList arguments{QStringLiteral("--no-pager"), QStringLiteral("-c"), QStringLiteral("color.ui=false")};
    arguments.append(command.arguments);
    process.setArguments(arguments);
    process.setWorkingDirectory(command.directory);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
    environment.remove(QStringLiteral("GIT_ASKPASS"));
    environment.remove(QStringLiteral("SSH_ASKPASS"));
    environment.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("never"));
    process.setProcessEnvironment(environment);
    if (cancellation.isCancellationRequested()) {
        result.cancelled = true; result.durationMs = timer.elapsed(); return result;
    }
    process.start();
    const qsizetype cap = command.diffPreview ? 8 * 1024 * 1024 : 16 * 1024 * 1024;
    if (!process.waitForStarted(3000)) {
        result.diagnostic = process.errorString(); result.durationMs = timer.elapsed(); return result;
    }
    result.processStarted = true;
    QElapsedTimer deadline; deadline.start();
    auto collect = [&] {
        const QByteArray out = process.readAllStandardOutput();
        const QByteArray err = process.readAllStandardError();
        const qsizetype remaining = qMax<qsizetype>(0, cap - result.standardOutput.size() - result.standardError.size());
        const qsizetype outKeep = qMin(remaining, out.size());
        result.standardOutput.append(out.first(outKeep));
        const qsizetype errKeep = qMin(qMax<qsizetype>(0, remaining - outKeep), err.size());
        result.standardError.append(err.first(errKeep));
        result.outputTruncated |= outKeep < out.size() || errKeep < err.size();
    };
    while (process.state() != QProcess::NotRunning) {
        collect();
        if (cancellation.isCancellationRequested()) {
            result.cancelled = true; process.terminate();
            if (!process.waitForFinished(500)) { process.kill(); process.waitForFinished(2000); }
            break;
        }
        if (deadline.elapsed() >= command.timeout.count()) {
            result.timedOut = true; process.terminate();
            if (!process.waitForFinished(500)) { process.kill(); process.waitForFinished(2000); }
            break;
        }
        process.waitForReadyRead(25);
    }
    collect();
    result.exitCode = process.exitCode();
    result.exitStatus = static_cast<int>(process.exitStatus());
    result.durationMs = timer.elapsed();
    if (result.outputTruncated) result.diagnostic = QStringLiteral("Output truncated at %1 bytes.").arg(cap);
    shared::security::RedactionService redactor;
    const QString safeError = redactor.redact(QString::fromUtf8(result.standardError));
    result.standardError = safeError.toUtf8();
    result.diagnostic = redactor.redact(result.diagnostic);
    qInfo().noquote() << QStringLiteral("Git operation=%1 durationMs=%2 exitCode=%3 truncated=%4 timeout=%5 cancelled=%6")
        .arg(command.operation).arg(result.durationMs).arg(result.exitCode)
        .arg(result.outputTruncated).arg(result.timedOut).arg(result.cancelled);
    return result;
}
}
