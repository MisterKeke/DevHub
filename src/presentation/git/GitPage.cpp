#include "GitPage.h"
#include "../../domain/errors/AppError.h"
#include "../../application/git/GitExecutableLocator.h"
#include "../../application/git/GitService.h"
#include "../../application/settings/SettingsService.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QShowEvent>
#include <atomic>
#include <memory>
#include <mutex>

namespace devhub::presentation::git {
namespace {
struct GitPageSnapshot final {
    application::git::GitRepositoryState status;
    QVector<application::git::GitCommit> history;
    QVector<application::git::GitBranchState> branches;
    QVector<application::git::GitBranchState> remoteBranches;
    QString historyError;
    QString branchesError;
    QString remoteBranchesError;
};
}
GitPage::GitPage(application::tasks::TaskScheduler& scheduler, QWidget* parent,
                 application::settings::SettingsService* settings)
    : QWidget(parent), m_scheduler(scheduler), m_settings(settings) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(24, 20, 24, 24);
    auto* top = new QHBoxLayout(); auto* heading = new QLabel(tr("Git"), this);
    heading->setStyleSheet(QStringLiteral("font-size: 18pt; font-weight: 600;"));
    m_state = new QLabel(tr("Select a workspace to inspect its repository."), this);
    m_refresh = new QPushButton(tr("Refresh"), this); m_cancel = new QPushButton(tr("Cancel"), this); m_cancel->setEnabled(false);
    auto* chooseGit = new QPushButton(tr("Choose Git…"), this);
    top->addWidget(heading); top->addSpacing(16); top->addWidget(m_state, 1); top->addWidget(chooseGit); top->addWidget(m_cancel); top->addWidget(m_refresh);
    layout->addLayout(top);
    m_partialErrors = new QLabel(this);
    m_partialErrors->setWordWrap(true);
    m_partialErrors->hide();
    layout->addWidget(m_partialErrors);
    auto* mutationActions = new QHBoxLayout();
    auto* stageSelected = new QPushButton(tr("Stage selected"), this);
    auto* stageAll = new QPushButton(tr("Stage all"), this);
    auto* unstageSelected = new QPushButton(tr("Unstage selected"), this);
    auto* commitButton = new QPushButton(tr("Commit…"), this);
    m_push = new QPushButton(tr("Push"), this);
    m_publishBranch = new QPushButton(tr("Publish Branch…"), this);
    m_mutationButtons = {stageSelected, stageAll, unstageSelected, commitButton, m_push, m_publishBranch};
    for (auto* button : m_mutationButtons) mutationActions->addWidget(button);
    mutationActions->addStretch(); layout->addLayout(mutationActions);
    auto* previewActions = new QHBoxLayout();
    auto* workingDiff = new QPushButton(tr("Working tree diff"), this);
    auto* stagedDiff = new QPushButton(tr("Staged diff"), this);
    previewActions->addStretch(); previewActions->addWidget(workingDiff); previewActions->addWidget(stagedDiff);
    layout->addLayout(previewActions);
    auto* split = new QSplitter(Qt::Horizontal, this);
    auto* tabs = new QTabWidget(split);
    m_status = new QListWidget(tabs); m_status->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto* historyTab = new QWidget(tabs);
    auto* historyLayout = new QVBoxLayout(historyTab);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    m_history = new QListWidget(historyTab);
    m_loadMore = new QPushButton(tr("Load older commits"), historyTab);
    m_loadMore->hide();
    historyLayout->addWidget(m_history, 1);
    historyLayout->addWidget(m_loadMore);
    m_branches = new QListWidget(tabs);
    tabs->addTab(m_status, tr("Changes")); tabs->addTab(historyTab, tr("History")); tabs->addTab(m_branches, tr("Branches"));
    m_diff = new QPlainTextEdit(split); m_diff->setReadOnly(true); m_diff->setPlaceholderText(tr("Select a commit or refresh to inspect changes."));
    split->addWidget(tabs); split->addWidget(m_diff); split->setStretchFactor(0, 1); split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    connect(m_refresh, &QPushButton::clicked, this, &GitPage::refresh);
    connect(stageSelected, &QPushButton::clicked, this, [this] {
        QStringList paths;
        for (auto* item : m_status->selectedItems()) paths.append(QString::fromUtf8(item->data(Qt::UserRole).toByteArray()));
        if (paths.isEmpty()) { m_state->setText(tr("Select one or more files first.")); return; }
        runMutation(QStringLiteral("stage"), paths);
    });
    connect(stageAll, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Stage all changes"), tr("Stage every change in this repository?")) == QMessageBox::Yes)
            runMutation(QStringLiteral("stage-all"), {});
    });
    connect(unstageSelected, &QPushButton::clicked, this, [this] {
        QStringList paths;
        for (auto* item : m_status->selectedItems()) paths.append(QString::fromUtf8(item->data(Qt::UserRole).toByteArray()));
        if (paths.isEmpty()) { m_state->setText(tr("Select one or more files first.")); return; }
        runMutation(QStringLiteral("unstage"), paths);
    });
    connect(commitButton, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const QString summary = QInputDialog::getText(this, tr("Commit changes"), tr("Commit summary (1–200 characters):"),
                                                       QLineEdit::Normal, {}, &accepted).trimmed();
        if (!accepted) return;
        if (summary.isEmpty() || summary.size() > 200) {
            QMessageBox::warning(this, tr("Invalid summary"), tr("Enter a commit summary between 1 and 200 characters.")); return;
        }
        runMutation(QStringLiteral("commit"), {summary});
    });
    connect(m_push, &QPushButton::clicked, this, [this] { requestPushPreview(false); });
    connect(m_publishBranch, &QPushButton::clicked, this, [this] { requestPushPreview(true); });
    connect(workingDiff, &QPushButton::clicked, this, [this] { showDiff({}, false); });
    connect(stagedDiff, &QPushButton::clicked, this, [this] { showDiff({}, true); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_activeTask.cancel(); });
    connect(chooseGit, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose Git executable"));
        if (path.isEmpty()) return;
        application::git::GitExecutableLocator locator; auto result = locator.validate(path);
        if (!result) { QMessageBox::warning(this, tr("Git not available"), result.error().userMessage); return; }
        locator.store(result.value());
        if (m_settings) {
            const auto saved = m_settings->setValue(QStringLiteral("git/executable"), result.value().path);
            if (!saved) { QMessageBox::warning(this, tr("Could not save Git setting"), saved.error().userMessage); return; }
        }
        m_state->setText(tr("Using Git %1").arg(result.value().version.raw)); refresh();
    });
    connect(m_history, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) showDiff(m_history->item(row)->data(Qt::UserRole).toString());
    });
    connect(m_loadMore, &QPushButton::clicked, this, &GitPage::loadOlderHistory);
}
void GitPage::setWorkspacePath(QString path, std::optional<QUuid> workspaceId) {
    if (m_path == path && m_workspaceId == workspaceId) return;
    m_activeTask.cancel();
    m_diffTask.cancel();
    m_historyTask.cancel();
    ++m_statusGeneration;
    ++m_diffGeneration;
    ++m_historyGeneration;
    setBusy(false);
    m_path = std::move(path);
    m_workspaceId = std::move(workspaceId);
    m_status->clear(); m_history->clear(); m_branches->clear(); m_diff->clear();
    m_loadMore->setText(tr("Load older commits"));
    m_loadMore->setEnabled(false);
    m_loadMore->hide();
    m_partialErrors->clear();
    m_partialErrors->hide();
    m_operationFeedback.clear();
    m_state->setText(m_path.isEmpty() ? tr("Select a workspace to inspect its repository.") : tr("Workspace selected. Reading Git status…"));
    if (!m_path.isEmpty()) refresh();
}
void GitPage::refreshNow() { refresh(); }
void GitPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_path.isEmpty() && m_refresh->isEnabled()) refresh();
}
void GitPage::setBusy(bool busy) {
    m_refresh->setEnabled(!busy); m_cancel->setEnabled(busy);
    for (auto* button : m_mutationButtons) button->setEnabled(!busy && !m_path.isEmpty());
}
QString GitPage::pushPreviewText(const application::git::GitPushPreview& preview) const {
    QStringList lines{
        tr("Repository: %1").arg(preview.repositoryRoot),
        tr("Branch: %1").arg(preview.localBranch),
        tr("Approved commit: %1").arg(preview.sourceCommit),
        tr("Configured remote: %1").arg(preview.remote),
        tr("Effective push URL: %1").arg(preview.effectivePushUrl),
        tr("Target branch: %1").arg(preview.targetBranch),
        tr("Tracking: %1").arg(preview.trackingDescription),
        tr("After a successful push, fetch %1 (%2) to refresh remote-tracking information.")
            .arg(preview.remote, preview.effectiveFetchUrl),
        tr("Remote state: %1").arg(preview.hasCachedRemoteTracking
            ? tr("cached remote-tracking data; not refreshed and no remote was contacted")
            : tr("not queried; outgoing commit count is unavailable")),
        tr("Uncommitted files are not included in this push.")};
    if (preview.fetchUrlDiffersFromPushUrl)
        lines.append(tr("The fetch and push URLs differ; fetched tracking data may not reflect the published branch."));
    if (preview.outgoingCountAvailable)
        lines.append(tr("Outgoing commits: %1").arg(preview.outgoingCommitCount));
    if (!preview.outgoingCommits.isEmpty()) {
        lines.append(tr("Recent outgoing commits:"));
        for (const auto& commit : preview.outgoingCommits)
            lines.append(QStringLiteral("  %1  %2").arg(commit.abbreviatedObjectId, commit.subject));
        if (preview.outgoingCommitsTruncated) lines.append(tr("  … additional commits omitted"));
    }
    return lines.join(QLatin1Char('\n'));
}
void GitPage::requestPushPreview(bool publishBranch) {
    if (m_path.isEmpty()) return;
    application::git::GitExecutableLocator locator;
    const auto executable = locator.locate();
    if (!executable) { m_state->setText(executable.error().userMessage); return; }
    const QString path = m_path, executablePath = executable.value().path;
    const quint64 generation = ++m_statusGeneration;
    application::tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("git-push-preview");
    request.workspaceId = m_workspaceId;
    request.generation = generation;
    setBusy(true);
    m_state->setText(publishBranch ? tr("Loading configured remotes…") : tr("Preparing push preview…"));
    if (!publishBranch) {
        preparePush({}, {}, false, generation);
        return;
    }
    m_activeTask = m_scheduler.submit<QStringList>(request,
        [path, executablePath](const application::tasks::TaskContext& context) {
            application::git::GitService service{application::git::GitCommandRunner(executablePath)};
            return service.configuredRemoteNames(path, context.cancellationToken);
        }, [this, generation, path, executablePath](shared::common::TaskOutcome<QStringList> outcome) {
            if (generation != m_statusGeneration) return;
            setBusy(false);
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                m_state->setText(tr("Publish preview cancelled.")); return;
            }
            auto result = std::get<shared::common::Result<QStringList>>(std::move(outcome));
            if (!result) {
                m_state->setText(result.error().userMessage);
                if (!m_operationFeedback.isEmpty()) { m_partialErrors->setText(m_operationFeedback); m_partialErrors->show(); }
                return;
            }
            if (result.value().isEmpty()) {
                m_state->setText(tr("This repository has no configured remotes. Add one before publishing a branch."));
                return;
            }
            bool accepted = false;
            const QString remote = QInputDialog::getItem(this, tr("Publish Branch"),
                tr("Choose a configured remote:"), result.value(), 0, false, &accepted);
            if (!accepted || remote.isEmpty()) { m_state->setText(tr("Publish cancelled before execution.")); return; }
            const QString target = QInputDialog::getText(this, tr("Publish Branch"),
                tr("Target branch on %1 (enter explicitly):").arg(remote), QLineEdit::Normal, {}, &accepted).trimmed();
            if (!accepted) { m_state->setText(tr("Publish cancelled before execution.")); return; }
            if (target.isEmpty()) { m_state->setText(tr("Enter the target branch name explicitly.")); return; }
            const auto trackingChoice = QMessageBox::question(this, tr("Set upstream tracking"),
                tr("After publication, set the current local branch to track %1/%2?").arg(remote, target),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
            if (trackingChoice == QMessageBox::Cancel) { m_state->setText(tr("Publish cancelled before execution.")); return; }
            preparePush(remote, target, trackingChoice == QMessageBox::Yes, generation);
        });
}
void GitPage::preparePush(QString remote, QString targetBranch, bool establishUpstream, quint64 generation) {
    if (m_path.isEmpty() || generation != m_statusGeneration) return;
    application::git::GitExecutableLocator locator;
    const auto executable = locator.locate();
    if (!executable) { setBusy(false); m_state->setText(executable.error().userMessage); return; }
    const QString path = m_path, executablePath = executable.value().path;
    application::tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("git-push-preview");
    request.workspaceId = m_workspaceId;
    request.generation = generation;
    setBusy(true);
    m_state->setText(tr("Preparing push preview…"));
    m_activeTask = m_scheduler.submit<application::git::GitPushPreview>(request,
        [path, executablePath, remote = std::move(remote), targetBranch = std::move(targetBranch), establishUpstream]
        (const application::tasks::TaskContext& context) {
            application::git::GitService service{application::git::GitCommandRunner(executablePath)};
            return service.preparePush(path, remote, targetBranch, establishUpstream, context.cancellationToken);
        }, [this, generation](shared::common::TaskOutcome<application::git::GitPushPreview> outcome) {
            if (generation != m_statusGeneration) return;
            setBusy(false);
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                m_state->setText(tr("Push preview cancelled.")); return;
            }
            auto result = std::get<shared::common::Result<application::git::GitPushPreview>>(std::move(outcome));
            if (!result) {
                m_operationFeedback = result.error().userMessage;
                m_partialErrors->setText(m_operationFeedback); m_partialErrors->show();
                m_state->setText(tr("Push preview could not be prepared.")); return;
            }
            const auto preview = std::move(result).value();
            if (QMessageBox::question(this, tr("Review Git push"), pushPreviewText(preview),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
                m_state->setText(tr("Push cancelled before execution.")); return;
            }
            executePush(preview);
        });
}
void GitPage::executePush(const application::git::GitPushPreview& preview) {
    if (m_path.isEmpty()) return;
    application::git::GitExecutableLocator locator;
    const auto executable = locator.locate();
    if (!executable) { m_state->setText(executable.error().userMessage); return; }
    const QString path = m_path, executablePath = executable.value().path;
    const quint64 generation = ++m_statusGeneration;
    application::tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("git-mutation");
    request.workspaceId = m_workspaceId;
    request.generation = generation;
    const auto mutationStarted = std::make_shared<std::atomic_bool>(false);
    const auto completedPush = std::make_shared<std::optional<application::git::GitPushResult>>();
    const auto completedPushMutex = std::make_shared<std::mutex>();
    setBusy(true);
    m_state->setText(tr("Pushing approved commit %1 to %2/%3…").arg(
        preview.sourceCommit.left(12), preview.remote, preview.targetBranch));
    m_activeTask = m_scheduler.submit<application::git::GitPushResult>(request,
        [path, executablePath, preview, mutationStarted, completedPush, completedPushMutex](const application::tasks::TaskContext& context) {
            context.reportProgress(QStringLiteral("Publishing approved Git commit"), 10);
            application::git::GitService service{application::git::GitCommandRunner(executablePath)};
            auto result = service.executePush(path, preview, context.cancellationToken,
                [mutationStarted] { mutationStarted->store(true); });
            if (result) {
                std::lock_guard<std::mutex> lock(*completedPushMutex);
                *completedPush = result.value();
            }
            return result;
        }, [this, generation, mutationStarted, completedPush, completedPushMutex](shared::common::TaskOutcome<application::git::GitPushResult> outcome) {
            if (generation != m_statusGeneration) return;
            setBusy(false);
            std::optional<application::git::GitPushResult> observed;
            {
                std::lock_guard<std::mutex> lock(*completedPushMutex);
                observed = *completedPush;
            }
            application::git::GitPushResult pushResult;
            bool hasPushResult = false;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                if (observed) { pushResult = *observed; hasPushResult = true; }
                else m_operationFeedback = mutationStarted->load()
                        ? tr("Push cancellation was requested after Git started. The remote may have accepted the commit; inspect the destination and create a fresh preview before retrying.")
                        : tr("Push was cancelled before Git started contacting the remote.");
            } else {
                auto result = std::get<shared::common::Result<application::git::GitPushResult>>(std::move(outcome));
                if (!result) {
                    m_operationFeedback = result.error().userMessage;
                    emit operationFailed(tr("Git push failed"), m_operationFeedback);
                }
                else {
                    pushResult = result.value(); hasPushResult = true;
                }
            }
            if (hasPushResult) {
                m_operationFeedback = pushResult.message;
                if (!pushResult.diagnostic.isEmpty()) m_operationFeedback += QStringLiteral("\n") + pushResult.diagnostic;
                if (!pushResult.fetchMessage.isEmpty()) m_operationFeedback += QStringLiteral("\n") + pushResult.fetchMessage;
                if (!pushResult.fetchDiagnostic.isEmpty()) m_operationFeedback += QStringLiteral("\n") + pushResult.fetchDiagnostic;
                if (pushResult.outcome != application::git::GitPushOutcome::Published
                    && pushResult.outcome != application::git::GitPushOutcome::AlreadyUpToDate)
                    emit operationFailed(tr("Git push did not complete"), m_operationFeedback);
            }
            m_partialErrors->setText(m_operationFeedback);
            m_partialErrors->show();
            m_state->setText(tr("Push operation finished; repository status is refreshing."));
            refresh();
        });
}
void GitPage::runMutation(QString operation, QStringList arguments) {
    if (m_path.isEmpty()) return;
    application::git::GitExecutableLocator locator; const auto executable = locator.locate();
    if (!executable) {
        m_state->setText(executable.error().userMessage);
        emit overviewUpdated(executable.error().userMessage, {}, tr("Git unavailable"), QStringLiteral("error"));
        return;
    }
    const QString path = m_path, executablePath = executable.value().path;
    application::tasks::TaskRequest request; request.moduleKey = QStringLiteral("git-mutation");
    const quint64 generation = ++m_statusGeneration;
    request.workspaceId = m_workspaceId; request.generation = generation;
    setBusy(true); m_state->setText(tr("Running %1…").arg(operation));
    m_activeTask = m_scheduler.submit<QString>(request,
        [path, executablePath, operation, arguments](const application::tasks::TaskContext& context) {
            application::git::GitService service{application::git::GitCommandRunner(executablePath)};
            shared::common::Result<application::git::GitCommandResult> result =
                shared::common::Result<application::git::GitCommandResult>::failure(
                    domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument,
                        domain::errors::ErrorCategory::Git, QStringLiteral("Unsupported Git action."), {}, operation));
            if (operation == QStringLiteral("stage")) result = service.stage(path, arguments, context.cancellationToken);
            else if (operation == QStringLiteral("stage-all")) result = service.stageAll(path, context.cancellationToken);
            else if (operation == QStringLiteral("unstage")) result = service.unstage(path, arguments, context.cancellationToken);
            else if (operation == QStringLiteral("commit") && arguments.size() == 1)
                result = service.commit(path, arguments.first(), context.cancellationToken);
            if (!result) return shared::common::Result<QString>::failure(result.error());
            return shared::common::Result<QString>::success(QString::fromUtf8(result.value().standardOutput));
        }, [this, operation, generation](shared::common::TaskOutcome<QString> outcome) {
            if (generation != m_statusGeneration) return;
            setBusy(false);
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                m_state->setText(tr("%1 cancelled.").arg(operation)); return;
            }
            auto result = std::get<shared::common::Result<QString>>(std::move(outcome));
            if (!result) {
                QMessageBox::warning(this, tr("Git operation failed"), result.error().userMessage);
                emit operationFailed(tr("Git operation failed"), result.error().userMessage);
            } else m_state->setText(tr("%1 completed.").arg(operation));
            refresh();
        });
}
void GitPage::refresh() {
    if (m_path.isEmpty()) { m_state->setText(tr("Select a workspace first.")); return; }
    application::git::GitExecutableLocator locator; const auto executable = locator.locate();
    if (!executable) {
        m_state->setText(executable.error().userMessage);
        emit overviewUpdated(executable.error().userMessage, {}, tr("Git unavailable"), QStringLiteral("error"));
        return;
    }
    locator.store(executable.value());
    m_diffTask.cancel();
    ++m_diffGeneration;
    m_historyTask.cancel();
    ++m_historyGeneration;
    m_loadMore->setText(tr("Load older commits"));
    m_loadMore->setEnabled(false);
    m_loadMore->hide();
    if (m_operationFeedback.isEmpty()) { m_partialErrors->clear(); m_partialErrors->hide(); }
    const QString path = m_path; const auto exePath = executable.value().path;
    application::tasks::TaskRequest request; request.moduleKey = QStringLiteral("git-read");
    const quint64 generation = ++m_statusGeneration;
    request.workspaceId = m_workspaceId; request.generation = generation;
    m_state->setText(tr("Reading repository…")); setBusy(true);
    m_activeTask = m_scheduler.submit<GitPageSnapshot>(request,
        [path, exePath](const application::tasks::TaskContext& context) {
            application::git::GitService service{application::git::GitCommandRunner(exePath)};
            auto status = service.status(path, context.cancellationToken);
            if (!status) return shared::common::Result<GitPageSnapshot>::failure(status.error());
            GitPageSnapshot snapshot;
            snapshot.status = std::move(status).value();
            if (snapshot.status.isRepository) {
                auto history = service.history(path, 0, 50, context.cancellationToken);
                if (history) snapshot.history = std::move(history).value();
                else snapshot.historyError = history.error().userMessage;
                auto branches = service.branches(path, false, context.cancellationToken);
                if (branches) snapshot.branches = std::move(branches).value();
                else snapshot.branchesError = branches.error().userMessage;
                auto remotes = service.branches(path, true, context.cancellationToken);
                if (remotes) snapshot.remoteBranches = std::move(remotes).value();
                else snapshot.remoteBranchesError = remotes.error().userMessage;
            }
            return shared::common::Result<GitPageSnapshot>::success(std::move(snapshot));
        }, [this, generation](shared::common::TaskOutcome<GitPageSnapshot> outcome) {
            if (generation != m_statusGeneration) return;
            setBusy(false);
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) {
                m_state->setText(tr("Git refresh cancelled.")); return;
            }
            auto result = std::get<shared::common::Result<GitPageSnapshot>>(std::move(outcome));
            if (!result) {
                m_state->setText(result.error().userMessage);
                if (!m_operationFeedback.isEmpty()) { m_partialErrors->setText(m_operationFeedback); m_partialErrors->show(); }
                return;
            }
            GitPageSnapshot snapshot = std::move(result).value();
            m_status->clear(); m_history->clear(); m_branches->clear(); m_diff->clear();
            m_partialErrors->clear(); m_partialErrors->hide();
            if (!snapshot.status.isRepository) {
                m_state->setText(tr("This workspace is not a Git repository."));
                if (!m_operationFeedback.isEmpty()) { m_partialErrors->setText(m_operationFeedback); m_partialErrors->show(); }
                emit overviewUpdated(tr("This folder is not a Git repository."), {}, tr("Not a repo"), QStringLiteral("warning"));
                return;
            }
            const auto& state = snapshot.status;
            const QString branchName = state.branch.isEmpty() ? tr("detached HEAD") : state.branch;
            QStringList details{tr("Branch  %1").arg(branchName)};
            if (state.upstream.isEmpty()) m_state->setText(tr("Branch: %1").arg(branchName));
            else {
                const QString aheadBehind = tr("Ahead %1 · Behind %2").arg(state.ahead).arg(state.behind);
                m_state->setText(tr("Branch: %1  ·  %2 (ahead %3, behind %4)").arg(
                    branchName, state.upstream).arg(state.ahead).arg(state.behind));
                details.append(tr("%1  %2").arg(state.upstream, aheadBehind));
            }
            for (const auto& file : state.files) {
                const QString path = QString::fromUtf8(file.pathBytes);
                const QString category = file.kind == application::git::GitFileKind::Unmerged ? tr("[conflict] ")
                    : file.kind == application::git::GitFileKind::Untracked ? tr("[untracked] ")
                    : file.kind == application::git::GitFileKind::Rename || file.kind == application::git::GitFileKind::Copy
                        ? tr("[rename/copy] ") : QString();
                auto* item = new QListWidgetItem(QStringLiteral("%1%2%3  %4").arg(
                    category, QString(file.indexStatus), QString(file.workTreeStatus), path), m_status);
                item->setData(Qt::UserRole, file.pathBytes);
                if (details.size() < (state.upstream.isEmpty() ? 4 : 5))
                    details.append(QStringLiteral("%1%2  %3").arg(QString(file.indexStatus), QString(file.workTreeStatus), path));
            }
            for (const auto& commit : snapshot.history) appendCommit(commit);
            for (const auto& branch : snapshot.branches)
                m_branches->addItem(QStringLiteral("%1 %2").arg(branch.current ? QStringLiteral("*") : QString(), branch.name));
            for (const auto& branch : snapshot.remoteBranches)
                m_branches->addItem(tr("Remote: %1").arg(branch.name));
            QStringList partialErrors;
            if (!snapshot.historyError.isEmpty()) partialErrors.append(tr("History: %1").arg(snapshot.historyError));
            if (!snapshot.branchesError.isEmpty()) partialErrors.append(tr("Local branches: %1").arg(snapshot.branchesError));
            if (!snapshot.remoteBranchesError.isEmpty()) partialErrors.append(tr("Remote branches: %1").arg(snapshot.remoteBranchesError));
            if (!m_operationFeedback.isEmpty()) partialErrors.append(m_operationFeedback);
            if (!partialErrors.isEmpty()) {
                m_partialErrors->setText(partialErrors.join(QStringLiteral(" · ")));
                m_partialErrors->show();
            }
            const int changeCount = m_status->count();
            emit overviewUpdated(tr("%1 · %2 changed file(s)").arg(branchName).arg(changeCount), details,
                changeCount == 0 ? tr("Clean") : tr("Changes"),
                changeCount == 0 ? QStringLiteral("success") : QStringLiteral("warning"));
            m_loadMore->setVisible(snapshot.historyError.isEmpty() && snapshot.history.size() == 50);
            m_loadMore->setEnabled(true);
            if (m_history->count() > 0) showDiff(m_history->item(0)->data(Qt::UserRole).toString());
        });
}
void GitPage::appendCommit(const application::git::GitCommit& commit) {
    auto* item = new QListWidgetItem(QStringLiteral("%1  %2  — %3").arg(
        commit.abbreviatedObjectId, commit.authoredAt.toString(Qt::ISODate), commit.subject), m_history);
    item->setData(Qt::UserRole, commit.objectId);
}
void GitPage::loadOlderHistory() {
    if (m_path.isEmpty() || !m_loadMore->isEnabled()) return;
    application::git::GitExecutableLocator locator;
    const auto executable = locator.locate();
    if (!executable) { m_state->setText(executable.error().userMessage); return; }
    const QString path = m_path, executablePath = executable.value().path;
    const int offset = m_history->count();
    const quint64 generation = ++m_historyGeneration;
    application::tasks::TaskRequest request;
    request.moduleKey = QStringLiteral("git-history");
    request.workspaceId = m_workspaceId;
    request.generation = generation;
    m_loadMore->setEnabled(false);
    m_loadMore->setText(tr("Loading older commits…"));
    m_historyTask = m_scheduler.submit<QVector<application::git::GitCommit>>(request,
        [path, executablePath, offset](const application::tasks::TaskContext& context) {
            application::git::GitService service{application::git::GitCommandRunner(executablePath)};
            return service.history(path, offset, 50, context.cancellationToken);
        }, [this, generation](shared::common::TaskOutcome<QVector<application::git::GitCommit>> outcome) {
            if (generation != m_historyGeneration) return;
            m_loadMore->setText(tr("Load older commits"));
            m_loadMore->setEnabled(true);
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome)) return;
            auto result = std::get<shared::common::Result<QVector<application::git::GitCommit>>>(std::move(outcome));
            if (!result) { m_state->setText(result.error().userMessage); return; }
            const auto& commits = result.value();
            for (const auto& commit : commits) appendCommit(commit);
            m_loadMore->setVisible(commits.size() == 50);
        });
}
void GitPage::showDiff(QString commit, bool staged) {
    if (m_path.isEmpty()) return;
    application::git::GitExecutableLocator locator; auto executable = locator.locate(); if (!executable) return;
    const QString path = m_path, exePath = executable.value().path;
    m_diffTask.cancel();
    application::tasks::TaskRequest request; request.moduleKey = QStringLiteral("git-diff");
    const quint64 generation = ++m_diffGeneration;
    request.workspaceId = m_workspaceId; request.generation = generation;
    m_diffTask = m_scheduler.submit<QString>(request,
        [path, exePath, commit, staged](const application::tasks::TaskContext& context) {
            application::git::GitService service{application::git::GitCommandRunner(exePath)};
            auto diff = service.diff(path, staged, commit, context.cancellationToken);
            if (!diff) return shared::common::Result<QString>::failure(diff.error());
            QString value = QString::fromUtf8(diff.value().bytes);
            if (diff.value().binary) value.prepend(QStringLiteral("[Binary diff]\n"));
            if (diff.value().truncated) value += QStringLiteral("\n[Diff preview truncated at 8 MB]\n");
            return shared::common::Result<QString>::success(std::move(value));
        }, [this, generation](shared::common::TaskOutcome<QString> outcome) {
            if (generation != m_diffGeneration) return;
            if (auto* result = std::get_if<shared::common::Result<QString>>(&outcome); result && *result)
                m_diff->setPlainText(result->value());
        });
}
}
