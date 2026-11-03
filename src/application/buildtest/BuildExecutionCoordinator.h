#pragma once

#include "BuildTypes.h"
#include "IBuildHistoryRepository.h"
#include "../services/ManagedProcess.h"
#include "../workspaces/WorkspaceContext.h"
#include "../../shared/security/RedactionService.h"

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

namespace devhub::application::buildtest {

struct CommandInvocation final {
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    QString reportPath;
    QString stage;
};

class BuildExecutionCoordinator final : public QObject
{
    Q_OBJECT
public:
    BuildExecutionCoordinator(IBuildHistoryRepository& history,
                              services::IProcessLauncher& processLauncher,
                              workspaces::WorkspaceContext& workspaceContext,
                              QString logDirectory,
                              const shared::security::RedactionService& redactor,
                              QObject* parent = nullptr);
    [[nodiscard]] shared::common::Result<CommandInvocation> resolve(const BuildProfile& profile,
                                                                     const QString& workspaceRoot,
                                                                     bool rebuild = false) const;
    [[nodiscard]] shared::common::Result<QString> start(const BuildProfile& profile,
                                                        const QString& workspaceRoot,
                                                        bool rebuild = false,
                                                        bool trustedNow = false,
                                                        QString taskId = {});
    void cancel();
    void cancelWorkspace(const QString& workspaceId);
    void beginShutdown(std::function<void()> completion);
    [[nodiscard]] bool running() const noexcept { return !m_current.id.isEmpty(); }
    [[nodiscard]] QString activeWorkspaceId() const { return m_current.workspaceId; }
    [[nodiscard]] const BuildRun& currentRun() const noexcept { return m_current; }

signals:
    void outputAvailable(QString runId, QByteArray bytes, bool standardError);
    void phaseChanged(QString phase);
    void runFinished(devhub::application::buildtest::BuildRun run);
    void runStartFailed(QString message);

private:
    void appendOutput(const QByteArray& bytes, bool standardError);
    void finish(int exitCode, int exitStatus);
    [[nodiscard]] bool pathWithinWorkspace(const QString& path, const QString& workspaceRoot,
                                           bool mustExist, QString* resolved) const;
    IBuildHistoryRepository& m_history;
    services::IProcessLauncher& m_processLauncher;
    workspaces::WorkspaceContext& m_workspaceContext;
    QString m_logDirectory;
    const shared::security::RedactionService& m_redactor;
    QPointer<services::IManagedProcess> m_process;
    BuildRun m_current;
    CommandInvocation m_invocation;
    QElapsedTimer m_elapsed;
    QTimer m_timeout;
    QFile m_logFile;
    QByteArray m_structuredOutput;
    QByteArray m_failureExcerpt;
    QString m_stage;
    bool m_requestedCancellation = false;
    bool m_timedOut = false;
    bool m_processStarted = false;
    bool m_cleanFirst = false;
    bool m_logTruncated = false;
    bool m_structuredOutputTruncated = false;
    bool m_logWriteFailed = false;
    qsizetype m_logBytes = 0;
    bool m_accepting = true;
};

} // namespace devhub::application::buildtest

Q_DECLARE_METATYPE(devhub::application::buildtest::BuildRun)
