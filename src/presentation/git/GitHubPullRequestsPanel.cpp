#include "GitHubPullRequestsPanel.h"

#include "../../application/github/GitHubService.h"
#include "../../application/planner/PlannerService.h"
#include "../../application/settings/SettingsService.h"
#include "../../domain/planner/PlannerTypes.h"
#include "../widgets/SafeMarkdownBrowser.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace devhub::presentation::git {
namespace {
QString safeStatus(const application::github::GitHubPullRequest& pullRequest)
{
    return application::github::pullRequestStatusText(pullRequest);
}

bool trustedGitHubWebUrl(const QString& value)
{
    const QUrl url(value, QUrl::StrictMode);
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0
        && url.userInfo().isEmpty() && (url.port(-1) == -1 || url.port() == 443);
}

QString dateText(const QDateTime& value)
{
    return value.isValid() ? QLocale().toString(value.toLocalTime(), QLocale::ShortFormat) : QObject::tr("Unknown");
}

QString elapsedText(const QDateTime& started, const QDateTime& completed)
{
    if (!started.isValid()) return QObject::tr("Duration unavailable");
    const qint64 seconds = qMax<qint64>(0, started.secsTo(completed.isValid() ? completed : QDateTime::currentDateTimeUtc()));
    return QObject::tr("%1m %2s").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}

GitHubPullRequestsPanel::GitHubPullRequestsPanel(application::github::GitHubService& service,
                                                 application::settings::SettingsService* settings,
                                                 application::planner::PlannerService* planner,
                                                 QWidget* parent)
    : QWidget(parent), m_service(service), m_settings(settings), m_planner(planner)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(8);
    auto* controls = new QHBoxLayout;
    m_repository = new QComboBox(this);
    m_repository->setObjectName(QStringLiteral("githubRepositorySelector"));
    m_repository->setMinimumWidth(210);
    m_stateFilter = new QComboBox(this);
    m_stateFilter->addItem(tr("Open"), QStringLiteral("open"));
    m_stateFilter->addItem(tr("Closed"), QStringLiteral("closed"));
    m_stateFilter->addItem(tr("All"), QStringLiteral("all"));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search Pull Requests…"));
    m_search->setClearButtonEnabled(true);
    m_refresh = new QPushButton(tr("Refresh"), this);
    m_loadMore = new QPushButton(tr("Load more"), this);
    m_loadMore->setEnabled(false);
    controls->addWidget(m_repository, 2);
    controls->addWidget(m_stateFilter);
    controls->addWidget(m_search, 2);
    controls->addWidget(m_refresh);
    controls->addWidget(m_loadMore);
    root->addLayout(controls);
    m_message = new QLabel(tr("Select a GitHub repository to browse Pull Requests."), this);
    m_message->setObjectName(QStringLiteral("githubPullRequestMessage"));
    m_message->setWordWrap(true);
    root->addWidget(m_message);

    auto* body = new QSplitter(Qt::Horizontal, this);
    m_table = new QTableWidget(body);
    m_table->setObjectName(QStringLiteral("githubPullRequestTable"));
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({tr("#"), tr("Title"), tr("Author"), tr("Branches"),
                                        tr("Local branch"), tr("Status"), tr("Review"), tr("Checks"), tr("Updated")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();

    auto* right = new QWidget(body);
    auto* detailLayout = new QVBoxLayout(right);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    auto* detailActions = new QHBoxLayout;
    m_detailHeading = new QLabel(tr("Select a Pull Request"), right);
    m_detailHeading->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 12pt;"));
    m_openOnGitHub = new QPushButton(tr("Open on GitHub"), right);
    m_openOnGitHub->setEnabled(false);
    m_linkTask = new QPushButton(tr("Link to Planner task…"), right);
    m_linkTask->setEnabled(false);
    detailActions->addWidget(m_detailHeading, 1);
    detailActions->addWidget(m_linkTask);
    detailActions->addWidget(m_openOnGitHub);
    detailLayout->addLayout(detailActions);

    m_detailTabs = new QTabWidget(right);
    auto* overviewPage = new QWidget(m_detailTabs);
    auto* overviewLayout = new QVBoxLayout(overviewPage);
    overviewLayout->setContentsMargins(0, 0, 0, 0);
    m_overview = new presentation::widgets::SafeMarkdownBrowser(overviewPage);
    m_overview->setPlaceholderText(tr("Pull Request details will appear here."));
    auto* plannerLinks = new QWidget(overviewPage);
    auto* plannerLinksLayout = new QHBoxLayout(plannerLinks);
    plannerLinksLayout->setContentsMargins(0, 4, 0, 0);
    auto* plannerLinkLabel = new QLabel(tr("Linked Planner tasks"), plannerLinks);
    m_linkedTasks = new QListWidget(plannerLinks);
    m_linkedTasks->setMaximumHeight(90);
    m_openLinkedTask = new QPushButton(tr("Open task"), plannerLinks);
    m_openLinkedTask->setEnabled(false);
    plannerLinksLayout->addWidget(plannerLinkLabel);
    plannerLinksLayout->addWidget(m_linkedTasks, 1);
    plannerLinksLayout->addWidget(m_openLinkedTask);
    overviewLayout->addWidget(m_overview, 1);
    overviewLayout->addWidget(plannerLinks);
    auto* filesPage = new QWidget(m_detailTabs);
    auto* filesLayout = new QVBoxLayout(filesPage);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    auto* filesSplit = new QSplitter(Qt::Vertical, filesPage);
    m_files = new QListWidget(filesSplit);
    m_filePatch = new QPlainTextEdit(filesSplit);
    m_filePatch->setReadOnly(true);
    m_filePatch->setPlaceholderText(tr("Select a changed file to inspect its available patch."));
    filesSplit->addWidget(m_files);
    filesSplit->addWidget(m_filePatch);
    filesLayout->addWidget(filesSplit);
    auto* reviewsPage = new QWidget(m_detailTabs);
    auto* reviewsLayout = new QVBoxLayout(reviewsPage);
    reviewsLayout->setContentsMargins(0, 0, 0, 0);
    m_reviews = new QListWidget(reviewsPage);
    m_reviewBody = new presentation::widgets::SafeMarkdownBrowser(reviewsPage);
    reviewsLayout->addWidget(m_reviews, 1);
    reviewsLayout->addWidget(m_reviewBody, 2);
    auto* checksPage = new QWidget(m_detailTabs);
    auto* checksLayout = new QVBoxLayout(checksPage);
    checksLayout->setContentsMargins(0, 0, 0, 0);
    m_checks = new QListWidget(checksPage);
    m_checkDetails = new QPlainTextEdit(checksPage);
    m_checkDetails->setReadOnly(true);
    auto* checkActions = new QHBoxLayout;
    m_loadJobLogs = new QPushButton(tr("Load selected job log"), checksPage);
    m_loadJobLogs->setEnabled(false);
    m_openCheckLink = new QPushButton(tr("Open in GitHub"), checksPage);
    m_openCheckLink->setEnabled(false);
    checkActions->addWidget(m_loadJobLogs);
    checkActions->addWidget(m_openCheckLink);
    checkActions->addStretch();
    checksLayout->addWidget(m_checks, 1);
    checksLayout->addLayout(checkActions);
    checksLayout->addWidget(m_checkDetails, 1);
    auto* ciHistoryPage = new QWidget(m_detailTabs);
    auto* ciHistoryLayout = new QVBoxLayout(ciHistoryPage);
    ciHistoryLayout->setContentsMargins(0, 0, 0, 0);
    m_ciHistoryMessage = new QLabel(tr("Repository workflow runs may not belong to the selected Pull Request."), ciHistoryPage);
    m_ciHistoryMessage->setWordWrap(true);
    m_ciHistoryTable = new QTableWidget(ciHistoryPage);
    m_ciHistoryTable->setObjectName(QStringLiteral("githubWorkflowHistoryTable"));
    m_ciHistoryTable->setColumnCount(6);
    m_ciHistoryTable->setHorizontalHeaderLabels({tr("Workflow"), tr("Status"), tr("Conclusion"),
        tr("Commit"), tr("Started"), tr("Duration")});
    m_ciHistoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ciHistoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ciHistoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ciHistoryTable->setSortingEnabled(true);
    m_ciHistoryTable->horizontalHeader()->setStretchLastSection(true);
    m_ciHistoryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_ciHistoryTable->verticalHeader()->hide();
    m_ciHistoryJobs = new QListWidget(ciHistoryPage);
    m_ciHistoryJobs->setObjectName(QStringLiteral("githubWorkflowJobs"));
    m_ciHistoryDetails = new QPlainTextEdit(ciHistoryPage);
    m_ciHistoryDetails->setReadOnly(true);
    m_ciHistoryDetails->setPlaceholderText(tr("Select a job to inspect its failed steps, then load its log if needed."));
    m_ciHistoryMore = new QPushButton(tr("Load more workflow runs"), ciHistoryPage);
    m_ciHistoryMore->setEnabled(false);
    auto* historyJobActions = new QHBoxLayout;
    m_loadHistoryJobLogs = new QPushButton(tr("Load selected job log"), ciHistoryPage);
    m_loadHistoryJobLogs->setEnabled(false);
    m_openHistoryJobLink = new QPushButton(tr("Open in GitHub"), ciHistoryPage);
    m_openHistoryJobLink->setEnabled(false);
    historyJobActions->addWidget(m_loadHistoryJobLogs);
    historyJobActions->addWidget(m_openHistoryJobLink);
    historyJobActions->addStretch();
    ciHistoryLayout->addWidget(m_ciHistoryMessage);
    ciHistoryLayout->addWidget(m_ciHistoryTable, 3);
    ciHistoryLayout->addWidget(m_ciHistoryJobs, 2);
    ciHistoryLayout->addLayout(historyJobActions);
    ciHistoryLayout->addWidget(m_ciHistoryDetails, 2);
    ciHistoryLayout->addWidget(m_ciHistoryMore, 0, Qt::AlignLeft);
    m_detailTabs->addTab(overviewPage, tr("Overview"));
    m_detailTabs->addTab(filesPage, tr("Files"));
    m_detailTabs->addTab(reviewsPage, tr("Reviews"));
    m_detailTabs->addTab(checksPage, tr("Checks"));
    m_detailTabs->addTab(ciHistoryPage, tr("CI History"));
    detailLayout->addWidget(m_detailTabs, 1);
    body->addWidget(m_table);
    body->addWidget(right);
    body->setStretchFactor(0, 3);
    body->setStretchFactor(1, 4);
    root->addWidget(body, 1);

    connect(m_refresh, &QPushButton::clicked, this, &GitHubPullRequestsPanel::refresh);
    connect(m_loadMore, &QPushButton::clicked, this, [this] { if (m_hasMore) loadPage(m_currentPage + 1); });
    connect(m_ciHistoryMore, &QPushButton::clicked, this, [this] {
        const auto repository = selectedRepository();
        if (!m_ciHistoryHasMore || !repository) return;
        ++m_ciHistoryPage;
        m_service.loadWorkflowHistory(*repository, m_ciHistoryPage);
    });
    connect(m_stateFilter, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_search, &QLineEdit::textChanged, this, &GitHubPullRequestsPanel::filterRows);
    connect(m_repository, &QComboBox::currentIndexChanged, this, [this] {
        m_service.cancelDashboardRequests();
        const auto selected = selectedRepository();
        if (!selected) {
            m_service.cancelDashboardRequests();
            m_pullRequests.clear();
            m_table->setRowCount(0);
            m_message->setText(m_repositories.isEmpty()
                ? tr("This workspace has no GitHub HTTPS or SSH remote.")
                : tr("Choose which GitHub remote this workspace uses for Pull Requests."));
            notifyMonitorTarget();
            return;
        }
        notifyMonitorTarget();
        if (m_settings && !workspaceKey().isEmpty()) {
            const auto saved = m_settings->setValue(QStringLiteral("github/remote"), selected->remoteName, workspaceKey());
            if (!saved) m_message->setText(saved.error().userMessage);
        }
        refresh();
    });
    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
                if (currentRow < 0 || !m_table->item(currentRow, 0)) return;
                const int number = m_table->item(currentRow, 0)->data(Qt::UserRole).toInt();
                if (number > 0) loadDetails(number);
            });
    connect(m_files, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) { m_filePatch->clear(); return; }
        const auto* item = m_files->item(row);
        const QString patch = item->data(Qt::UserRole).toString();
        m_filePatch->setPlainText(patch.isEmpty() ? tr("Patch unavailable for this file.") : patch);
    });
    connect(m_reviews, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) { m_reviewBody->setSourceText(QString{}); return; }
        m_reviewBody->setSourceText(m_reviews->item(row)->data(Qt::UserRole).toString());
    });
    connect(m_checks, &QListWidget::currentRowChanged, this, [this](int row) {
        m_checkLogJobId = 0;
        m_loadJobLogs->setEnabled(false);
        m_openCheckLink->setEnabled(false);
        if (row < 0) { m_checkDetails->clear(); return; }
        const auto* item = m_checks->item(row);
        m_checkDetails->setPlainText(item->data(Qt::UserRole).toString());
        m_checkLogJobId = item->data(Qt::UserRole + 3).toLongLong();
        m_loadJobLogs->setEnabled(m_checkLogJobId > 0);
        m_openCheckLink->setEnabled(trustedGitHubWebUrl(item->data(Qt::UserRole + 2).toString()));
    });
    connect(m_loadJobLogs, &QPushButton::clicked, this, [this] {
        const auto repository = selectedRepository();
        if (!repository || m_checkLogJobId <= 0) return;
        m_checkDetails->setPlainText(tr("Downloading a bounded GitHub Actions log…"));
        m_loadJobLogs->setEnabled(false);
        m_service.loadWorkflowJobLogs(*repository, m_checkLogJobId);
    });
    connect(m_openCheckLink, &QPushButton::clicked, this, [this] {
        const auto* item = m_checks->currentItem();
        if (!item) return;
        const QUrl url(item->data(Qt::UserRole + 2).toString(), QUrl::StrictMode);
        if (trustedGitHubWebUrl(url.toString())) QDesktopServices::openUrl(url);
    });
    connect(m_detailTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index != m_detailTabs->indexOf(m_ciHistoryTable->parentWidget()) || m_ciHistoryPage != 0) return;
        const auto repository = selectedRepository();
        if (!repository) return;
        m_ciHistoryPage = 1;
        m_ciHistoryMessage->setText(tr("Loading recent repository workflow runs…"));
        m_service.loadWorkflowHistory(*repository, m_ciHistoryPage);
    });
    connect(m_ciHistoryTable, &QTableWidget::currentCellChanged, this,
        [this](int row, int, int, int) {
            if (row < 0 || !m_ciHistoryTable->item(row, 0)) return;
            const qint64 runId = m_ciHistoryTable->item(row, 0)->data(Qt::UserRole).toLongLong();
            const auto repository = selectedRepository();
            if (runId > 0 && repository) {
                m_currentHistoryRunId = runId;
                m_ciHistoryJobs->clear();
                m_ciHistoryDetails->setPlainText(tr("Loading jobs for workflow run %1…").arg(runId));
                m_service.loadWorkflowRunJobs(*repository, runId);
            }
        });
    connect(m_ciHistoryTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (!m_ciHistoryTable->item(row, 0)) return;
        const QUrl url(m_ciHistoryTable->item(row, 0)->data(Qt::UserRole + 1).toString(), QUrl::StrictMode);
        if (trustedGitHubWebUrl(url.toString())) QDesktopServices::openUrl(url);
    });
    connect(m_ciHistoryJobs, &QListWidget::currentRowChanged, this, [this](int row) {
        m_historyLogJobId = 0;
        m_loadHistoryJobLogs->setEnabled(false);
        m_openHistoryJobLink->setEnabled(false);
        if (row < 0) { m_ciHistoryDetails->clear(); return; }
        const auto* item = m_ciHistoryJobs->item(row);
        m_ciHistoryDetails->setPlainText(item->data(Qt::UserRole).toString());
        m_historyLogJobId = item->data(Qt::UserRole + 1).toLongLong();
        m_loadHistoryJobLogs->setEnabled(m_historyLogJobId > 0);
        m_openHistoryJobLink->setEnabled(trustedGitHubWebUrl(item->data(Qt::UserRole + 2).toString()));
    });
    connect(m_loadHistoryJobLogs, &QPushButton::clicked, this, [this] {
        const auto repository = selectedRepository();
        if (!repository || m_historyLogJobId <= 0) return;
        m_ciHistoryDetails->setPlainText(tr("Downloading a bounded GitHub Actions log…"));
        m_loadHistoryJobLogs->setEnabled(false);
        m_service.loadWorkflowJobLogs(*repository, m_historyLogJobId);
    });
    connect(m_openHistoryJobLink, &QPushButton::clicked, this, [this] {
        const auto* item = m_ciHistoryJobs->currentItem();
        if (!item) return;
        const QUrl url(item->data(Qt::UserRole + 2).toString(), QUrl::StrictMode);
        if (trustedGitHubWebUrl(url.toString())) QDesktopServices::openUrl(url);
    });
    connect(m_openOnGitHub, &QPushButton::clicked, this, [this] {
        for (const auto& pullRequest : m_pullRequests)
            if (pullRequest.number == m_selectedNumber && trustedGitHubWebUrl(pullRequest.htmlUrl))
                QDesktopServices::openUrl(QUrl(pullRequest.htmlUrl));
    });
    connect(m_linkTask, &QPushButton::clicked, this, &GitHubPullRequestsPanel::linkToPlannerTask);
    connect(m_openLinkedTask, &QPushButton::clicked, this, [this] {
        const auto* item = m_linkedTasks->currentItem();
        if (item) emit openPlannerTaskRequested(item->data(Qt::UserRole).toString());
    });
    const auto openTrustedLink = [](const QUrl& url) {
        if (trustedGitHubWebUrl(url.toString())) QDesktopServices::openUrl(url);
    };
    connect(m_overview, &QTextBrowser::anchorClicked, this, openTrustedLink);
    connect(m_reviewBody, &QTextBrowser::anchorClicked, this, openTrustedLink);

    connect(&m_service, &application::github::GitHubService::connectionChanged, this,
        [this](const application::github::GitHubConnection& state) {
            if (state.state != application::github::GitHubConnection::State::Connected) {
                m_message->setText(state.message.isEmpty()
                    ? tr("Connect GitHub in Settings → Integrations → GitHub to use this dashboard.") : state.message);
                m_refresh->setEnabled(false);
                m_loadMore->setEnabled(false);
            } else {
                m_refresh->setEnabled(selectedRepository().has_value());
                if (m_message->text().startsWith(tr("Connect GitHub"))) m_message->setText(tr("Select a repository and refresh."));
            }
        });
    connect(&m_service, &application::github::GitHubService::pullRequestsLoaded, this,
        [this](QVector<application::github::GitHubPullRequest> pullRequests, bool hasMore, int page, QString error) {
            if (page != m_currentPage) return;
            m_refresh->setEnabled(selectedRepository().has_value());
            m_loadMore->setEnabled(false);
            if (!error.isEmpty()) {
                m_message->setText(error);
                return;
            }
            m_hasMore = hasMore;
            m_loadMore->setEnabled(hasMore);
            m_loadMore->setVisible(hasMore);
            if (page == 1) {
                m_pullRequests.clear();
                m_table->setRowCount(0);
            }
            const bool sorting = m_table->isSortingEnabled();
            m_table->setSortingEnabled(false);
            for (const auto& pullRequest : pullRequests) {
                m_pullRequests.push_back(pullRequest);
                const int row = m_table->rowCount();
                m_table->insertRow(row);
                auto* number = new QTableWidgetItem(QStringLiteral("#%1").arg(pullRequest.number));
                number->setData(Qt::UserRole, pullRequest.number);
                m_table->setItem(row, 0, number);
                m_table->setItem(row, 1, new QTableWidgetItem(pullRequest.title));
                m_table->setItem(row, 2, new QTableWidgetItem(pullRequest.author));
                m_table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("%1 → %2").arg(pullRequest.headBranch, pullRequest.baseBranch)));
                m_table->setItem(row, 4, new QTableWidgetItem(
                    !m_localBranch.isEmpty() && pullRequest.headBranch == m_localBranch ? tr("Current") : QString{}));
                m_table->setItem(row, 5, new QTableWidgetItem(safeStatus(pullRequest)));
                m_table->setItem(row, 6, new QTableWidgetItem(tr("Select to load")));
                m_table->setItem(row, 7, new QTableWidgetItem(tr("Select to load")));
                m_table->setItem(row, 8, new QTableWidgetItem(dateText(pullRequest.updatedAtUtc)));
            }
            m_table->setSortingEnabled(sorting);
            m_message->setText(m_pullRequests.isEmpty()
                ? tr("No Pull Requests match this filter.")
                : tr("Showing %1 Pull Request(s). Select one to load reviews, changed files, and CI details.")
                      .arg(m_pullRequests.size()));
            filterRows();
        });
    connect(&m_service, &application::github::GitHubService::pullRequestDetailsLoaded, this,
        [this](application::github::GitHubPullRequestDetails details, QString error) {
            if (!error.isEmpty()) { m_message->setText(error); return; }
            if (details.pullRequest.number != m_selectedNumber) return;
            updateSelectedStatus(details);
            renderDetails(details);
        });
    connect(&m_service, &application::github::GitHubService::workflowRunJobsLoaded, this,
        [this](qint64 runId, QVector<application::github::GitHubWorkflowJob> jobs, QString error) {
            if (runId != m_currentHistoryRunId) return;
            if (!error.isEmpty()) {
                m_ciHistoryJobs->clear();
                m_ciHistoryDetails->setPlainText(error);
                return;
            }
            m_ciHistoryJobs->clear();
            for (const auto& job : jobs) {
                auto* item = new QListWidgetItem(tr("%1 — %2")
                    .arg(job.name, application::github::checkRunStatusText(job.status, job.conclusion)), m_ciHistoryJobs);
                QStringList lines{tr("Job: %1").arg(job.name),
                    tr("Status: %1").arg(job.status),
                    tr("Conclusion: %1").arg(job.conclusion.isEmpty() ? tr("unknown") : job.conclusion),
                    tr("Started: %1").arg(dateText(job.startedAtUtc)),
                    tr("Completed: %1").arg(dateText(job.completedAtUtc))};
                for (const auto& step : job.failedSteps) lines << tr("Failed step: %1").arg(step);
                item->setData(Qt::UserRole, lines.join(QLatin1Char('\n')));
                item->setData(Qt::UserRole + 1, job.conclusion == QStringLiteral("failure") ? job.id : 0);
                item->setData(Qt::UserRole + 2, job.htmlUrl);
            }
            if (jobs.isEmpty()) m_ciHistoryDetails->setPlainText(tr("No jobs were returned for this workflow run."));
            else m_ciHistoryJobs->setCurrentRow(0);
        });
    connect(&m_service, &application::github::GitHubService::workflowJobLogsLoaded, this,
        [this](qint64 jobId, QString text, bool truncated, QString error) {
            const QString result = !error.isEmpty() ? error
                : text + (truncated ? tr("\n\n[Log excerpt truncated by DevHub's display limit.]\n") : QString{});
            if (jobId == m_checkLogJobId) {
                m_checkDetails->setPlainText(result);
                m_loadJobLogs->setEnabled(true);
            }
            if (jobId == m_historyLogJobId) {
                m_ciHistoryDetails->setPlainText(result);
                m_loadHistoryJobLogs->setEnabled(true);
            }
        });
    connect(&m_service, &application::github::GitHubService::workflowHistoryLoaded, this,
        [this](QVector<application::github::GitHubWorkflowRun> runs, bool hasMore, int page, QString error) {
            if (page != m_ciHistoryPage) return;
            if (!error.isEmpty()) {
                m_ciHistoryMessage->setText(error);
                m_ciHistoryMore->setEnabled(false);
                return;
            }
            const bool sorting = m_ciHistoryTable->isSortingEnabled();
            m_ciHistoryTable->setSortingEnabled(false);
            for (const auto& run : runs) {
                const int row = m_ciHistoryTable->rowCount();
                m_ciHistoryTable->insertRow(row);
                auto* name = new QTableWidgetItem(run.name);
                name->setData(Qt::UserRole, run.id);
                name->setData(Qt::UserRole + 1, run.htmlUrl);
                m_ciHistoryTable->setItem(row, 0, name);
                m_ciHistoryTable->setItem(row, 1, new QTableWidgetItem(run.status));
                m_ciHistoryTable->setItem(row, 2, new QTableWidgetItem(run.conclusion.isEmpty() ? tr("—") : run.conclusion));
                m_ciHistoryTable->setItem(row, 3, new QTableWidgetItem(run.headSha.left(12)));
                m_ciHistoryTable->setItem(row, 4, new QTableWidgetItem(dateText(run.startedAtUtc)));
                m_ciHistoryTable->setItem(row, 5, new QTableWidgetItem(elapsedText(run.startedAtUtc, run.completedAtUtc)));
            }
            m_ciHistoryTable->setSortingEnabled(sorting);
            m_ciHistoryHasMore = hasMore;
            m_ciHistoryMore->setEnabled(hasMore);
            m_ciHistoryMore->setVisible(hasMore);
            m_ciHistoryMessage->setText(tr("Repository history: %1 workflow run(s). A run belongs to a Pull Request only when its commit matches that Pull Request's head commit.")
                .arg(m_ciHistoryTable->rowCount()));
        });
}

void GitHubPullRequestsPanel::setWorkspace(std::optional<QUuid> workspaceId)
{
    if (m_workspaceId == workspaceId) return;
    m_service.cancelDashboardRequests();
    m_workspaceId = workspaceId;
    m_repositories.clear();
    m_pullRequests.clear();
    m_table->setRowCount(0);
    m_ciHistoryPage = 0;
    m_ciHistoryHasMore = false;
    m_ciHistoryTable->setRowCount(0);
    m_ciHistoryJobs->clear();
    m_ciHistoryMore->setEnabled(false);
    m_ciHistoryDetails->clear();
    m_currentHistoryRunId = 0;
    m_checkLogJobId = 0;
    m_historyLogJobId = 0;
    m_repository->clear();
    m_repository->addItem(tr("No GitHub remote"));
    m_message->setText(workspaceId ? tr("Reading Git remotes…") : tr("Select a workspace to inspect Pull Requests."));
    m_selectedNumber = 0;
    m_detailHeading->setText(tr("Select a Pull Request"));
    m_openOnGitHub->setEnabled(false);
    m_linkTask->setEnabled(false);
    notifyMonitorTarget();
}

void GitHubPullRequestsPanel::setRepositoryError(QString error)
{
    if (!error.isEmpty()) m_message->setText(std::move(error));
    notifyMonitorTarget();
}

void GitHubPullRequestsPanel::setLocalBranch(QString branch)
{
    m_localBranch = std::move(branch);
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const int number = m_table->item(row, 0)->data(Qt::UserRole).toInt();
        const auto pullRequest = std::find_if(m_pullRequests.cbegin(), m_pullRequests.cend(), [number](const auto& pr) {
            return pr.number == number;
        });
        if (pullRequest != m_pullRequests.cend())
            m_table->item(row, 4)->setText(!m_localBranch.isEmpty() && pullRequest->headBranch == m_localBranch
                ? tr("Current") : QString{});
    }
}

void GitHubPullRequestsPanel::setRepositories(QVector<application::github::GitHubRepository> repositories)
{
    const QString previous = selectedRepository() ? selectedRepository()->remoteName : QString{};
    m_repositories = std::move(repositories);
    m_ciHistoryPage = 0;
    m_ciHistoryHasMore = false;
    m_ciHistoryTable->setRowCount(0);
    m_ciHistoryJobs->clear();
    m_ciHistoryMore->setEnabled(false);
    const QSignalBlocker blocker(m_repository);
    m_repository->clear();
    m_repository->addItem(m_repositories.isEmpty() ? tr("No GitHub remote") : tr("Choose GitHub remote…"));
    for (const auto& repository : m_repositories)
        m_repository->addItem(QStringLiteral("%1  ·  %2").arg(repository.remoteName, repository.fullName()),
                              QVariantMap{{QStringLiteral("remote"), repository.remoteName},
                                          {QStringLiteral("owner"), repository.owner},
                                          {QStringLiteral("name"), repository.name}});
    const QString configured = m_settings && !workspaceKey().isEmpty()
        ? m_settings->value(QStringLiteral("github/remote"), workspaceKey()).toString() : QString{};
    const QString desired = !configured.isEmpty() ? configured : previous;
    int selected = 0;
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (m_repositories.at(i).remoteName == desired) { selected = i + 1; break; }
    }
    if (selected == 0 && m_repositories.size() == 1) selected = 1;
    m_repository->setCurrentIndex(selected);
    m_repository->setEnabled(m_repositories.size() > 0);
    notifyMonitorTarget();
    m_refresh->setEnabled(selected > 0
        && m_service.connection().state == application::github::GitHubConnection::State::Connected);
    if (selected == 0) {
        m_message->setText(m_repositories.isEmpty()
            ? tr("This workspace has no GitHub HTTPS or SSH remote.")
            : tr("Choose which GitHub remote is associated with this workspace."));
        return;
    }
    if (m_settings && configured.isEmpty() && !workspaceKey().isEmpty()) {
        const auto saved = m_settings->setValue(QStringLiteral("github/remote"), m_repositories.at(selected - 1).remoteName, workspaceKey());
        if (!saved) m_message->setText(saved.error().userMessage);
    }
    if (m_service.connection().state == application::github::GitHubConnection::State::Connected) refresh();
    else m_message->setText(tr("Connect GitHub in Settings → Integrations → GitHub to load Pull Requests."));
}

void GitHubPullRequestsPanel::refresh()
{
    if (m_service.connection().state != application::github::GitHubConnection::State::Connected) {
        m_message->setText(tr("Connect GitHub in Settings → Integrations → GitHub to use this dashboard."));
        return;
    }
    if (!selectedRepository()) {
        m_message->setText(m_repositories.isEmpty()
            ? tr("This workspace has no GitHub remote.") : tr("Choose the GitHub remote for this workspace."));
        return;
    }
    m_service.cancelDashboardRequests();
    m_pullRequests.clear();
    m_table->setRowCount(0);
    m_ciHistoryPage = 0;
    m_ciHistoryHasMore = false;
    m_ciHistoryTable->setRowCount(0);
    m_ciHistoryDetails->clear();
    m_currentHistoryRunId = 0;
    m_checkLogJobId = 0;
    m_historyLogJobId = 0;
    m_currentPage = 0;
    m_hasMore = false;
    m_loadMore->hide();
    loadPage(1);
}

void GitHubPullRequestsPanel::loadPage(int page)
{
    const auto repository = selectedRepository();
    if (!repository) return;
    m_currentPage = page;
    m_message->setText(tr("Loading Pull Requests from %1…").arg(repository->fullName()));
    m_refresh->setEnabled(false);
    m_loadMore->setEnabled(false);
    m_service.loadPullRequests(*repository, m_stateFilter->currentData().toString(), page);
}

void GitHubPullRequestsPanel::loadDetails(int number)
{
    const auto repository = selectedRepository();
    if (!repository) return;
    m_service.cancelDashboardRequests();
    m_selectedNumber = number;
    m_detailHeading->setText(tr("Loading Pull Request #%1…").arg(number));
    m_openOnGitHub->setEnabled(false);
    m_linkTask->setEnabled(false);
    m_overview->setSourceText(QString{});
    m_service.loadPullRequestDetails(*repository, number);
}

void GitHubPullRequestsPanel::renderDetails(const application::github::GitHubPullRequestDetails& details)
{
    const auto& pr = details.pullRequest;
    m_detailHeading->setText(tr("#%1 · %2").arg(pr.number).arg(pr.title));
    m_openOnGitHub->setEnabled(trustedGitHubWebUrl(pr.htmlUrl));
    m_linkTask->setEnabled(m_planner && !workspaceKey().isEmpty());
    m_linkedTasks->clear();
    m_openLinkedTask->setEnabled(false);
    QString summary = tr("**%1** · %2\n\n%3 → %4\n\n%5 additions, %6 deletions, %7 changed file(s)\n\nCreated %8 · Updated %9\n\n")
        .arg(pr.author, safeStatus(pr), pr.headBranch, pr.baseBranch)
        .arg(pr.additions).arg(pr.deletions).arg(pr.changedFiles)
        .arg(dateText(pr.createdAtUtc), dateText(pr.updatedAtUtc));
    if (pr.merged) summary += tr("Merged\n\n");
    if (!details.partialErrors.isEmpty()) summary += tr("Some sections are unavailable: %1\n\n").arg(details.partialErrors.join(QStringLiteral(" · ")));
    summary += tr("Head commit: %1\n\nBase commit: %2\n\nMerge state: %3\n\n")
        .arg(pr.headSha, pr.baseSha, pr.mergeableState.isEmpty() ? tr("unknown") : pr.mergeableState);
    summary += pr.body;
    m_overview->setSourceText(summary);
    if (m_planner && !workspaceKey().isEmpty()) {
        const auto tasks = m_planner->listTasks(workspaceKey(), {});
        if (tasks) {
            for (const auto& task : tasks.value()) {
                if (task.githubPullRequestUrl != pr.htmlUrl) continue;
                auto* item = new QListWidgetItem(task.title, m_linkedTasks);
                item->setData(Qt::UserRole, task.id);
            }
        }
    }
    if (m_linkedTasks->count() == 0) m_linkedTasks->addItem(tr("No Planner task is linked to this Pull Request."));
    else {
        m_linkedTasks->setCurrentRow(0);
        m_openLinkedTask->setEnabled(true);
    }

    m_files->clear();
    m_filePatch->clear();
    for (const auto& file : details.files) {
        auto* item = new QListWidgetItem(tr("%1  ·  %2  +%3/−%4")
            .arg(file.status, file.path).arg(file.additions).arg(file.deletions), m_files);
        item->setData(Qt::UserRole, file.patchUnavailable ? QString{} : file.patch);
    }
    m_reviews->clear();
    m_reviewBody->setSourceText(QString{});
    for (const auto& review : details.reviews) {
        auto* item = new QListWidgetItem(tr("%1 — %2 — %3")
            .arg(review.reviewer.isEmpty() ? tr("Unknown reviewer") : review.reviewer,
                 review.state, dateText(review.submittedAtUtc)), m_reviews);
        item->setData(Qt::UserRole, review.body);
    }
    for (const auto& comment : details.comments) {
        auto* item = new QListWidgetItem(tr("Comment — %1 — %2")
            .arg(comment.author.isEmpty() ? tr("Unknown author") : comment.author,
                 dateText(comment.createdAtUtc)), m_reviews);
        item->setData(Qt::UserRole, comment.body);
    }
    m_checks->clear();
    m_checkDetails->clear();
    for (const auto& check : details.checks) {
        const QString text = application::github::checkRunStatusText(check.status, check.conclusion);
        auto* item = new QListWidgetItem(tr("Check: %1 — %2 (%3)")
            .arg(check.name, check.status, check.conclusion.isEmpty() ? tr("no conclusion") : check.conclusion), m_checks);
        item->setData(Qt::UserRole, tr("State: %1\nConclusion: %2\nCommit: %3\nStarted: %4\nCompleted: %5\nDuration: %6")
            .arg(check.status, check.conclusion.isEmpty() ? tr("unknown") : check.conclusion, check.headSha,
                 dateText(check.startedAtUtc), dateText(check.completedAtUtc), elapsedText(check.startedAtUtc, check.completedAtUtc)));
        item->setToolTip(text);
    }
    for (const auto& run : details.workflowRuns) {
        auto* item = new QListWidgetItem(tr("Actions: %1 — %2 (%3)")
            .arg(run.name, run.status, run.conclusion.isEmpty() ? tr("no conclusion") : run.conclusion), m_checks);
        item->setData(Qt::UserRole, tr("Workflow: %1\nState: %2\nConclusion: %3\nCommit: %4\nStarted: %5\nUpdated: %6\nDuration: %7\nRun ID: %8")
            .arg(run.name, run.status, run.conclusion.isEmpty() ? tr("unknown") : run.conclusion, run.headSha,
                 dateText(run.startedAtUtc), dateText(run.completedAtUtc), elapsedText(run.startedAtUtc, run.completedAtUtc))
            .arg(run.id));
        item->setData(Qt::UserRole + 1, run.id);
        item->setData(Qt::UserRole + 2, run.htmlUrl);
    }
    for (const auto& job : details.failedJobs) {
        auto* item = new QListWidgetItem(tr("Failed job: %1").arg(job.name), m_checks);
        QStringList steps;
        for (const auto& step : job.failedSteps) steps << tr("Failed step: %1").arg(step);
        item->setData(Qt::UserRole, tr("Job: %1\nConclusion: %2\nStarted: %3\nCompleted: %4\n%5")
            .arg(job.name, job.conclusion, dateText(job.startedAtUtc), dateText(job.completedAtUtc), steps.join(QLatin1Char('\n'))));
        item->setData(Qt::UserRole + 2, job.htmlUrl);
        item->setData(Qt::UserRole + 3, job.id);
    }
    if (m_checks->count() == 0) m_checks->addItem(tr("No check runs or workflow runs were returned for this commit."));
    if (m_files->count() == 0) m_files->addItem(tr("No changed-file details were returned."));
    if (m_reviews->count() == 0) m_reviews->addItem(tr("No review or discussion information was returned."));
}

void GitHubPullRequestsPanel::updateSelectedStatus(const application::github::GitHubPullRequestDetails& details)
{
    QString reviewStatus = tr("Review pending");
    bool approved = false;
    for (const auto& review : details.reviews) {
        if (review.state == QStringLiteral("CHANGES_REQUESTED")) { reviewStatus = tr("Changes requested"); break; }
        if (review.state == QStringLiteral("APPROVED")) approved = true;
    }
    if (reviewStatus == tr("Review pending") && approved) reviewStatus = tr("Approved");
    QString checkStatus = tr("Unknown");
    bool failed = false, inProgress = false, success = false;
    const auto accumulate = [&failed, &inProgress, &success](const QString& status, const QString& conclusion) {
        const QString text = application::github::checkRunStatusText(status, conclusion);
        failed = failed || text == QObject::tr("Failure");
        inProgress = inProgress || text == QObject::tr("In progress") || text == QObject::tr("Queued")
            || text == QObject::tr("Pending");
        success = success || text == QObject::tr("Success");
    };
    for (const auto& check : details.checks) accumulate(check.status, check.conclusion);
    for (const auto& run : details.workflowRuns) accumulate(run.status, run.conclusion);
    if (failed) checkStatus = tr("Failure");
    else if (inProgress) checkStatus = tr("In progress");
    else if (success) checkStatus = tr("Success");
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* number = m_table->item(row, 0);
        if (number && number->data(Qt::UserRole).toInt() == details.pullRequest.number) {
            m_table->item(row, 6)->setText(reviewStatus);
            m_table->item(row, 7)->setText(checkStatus);
            break;
        }
    }
}

void GitHubPullRequestsPanel::filterRows()
{
    const QString query = m_search->text().trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QStringList fields;
        for (int column = 0; column < m_table->columnCount(); ++column)
            if (m_table->item(row, column)) fields << m_table->item(row, column)->text();
        m_table->setRowHidden(row, !query.isEmpty() && !fields.join(QLatin1Char(' ')).contains(query, Qt::CaseInsensitive));
    }
}

void GitHubPullRequestsPanel::linkToPlannerTask()
{
    if (!m_planner || workspaceKey().isEmpty()) return;
    const auto selected = std::find_if(m_pullRequests.cbegin(), m_pullRequests.cend(), [this](const auto& pr) {
        return pr.number == m_selectedNumber;
    });
    if (selected == m_pullRequests.cend() || !trustedGitHubWebUrl(selected->htmlUrl)) return;
    auto tasks = m_planner->listTasks(workspaceKey(), {});
    if (!tasks) {
        QMessageBox::warning(this, tr("Planner task unavailable"), tasks.error().userMessage);
        return;
    }
    if (tasks.value().isEmpty()) {
        QMessageBox::information(this, tr("No Planner tasks"), tr("Create a Planner task in this workspace before linking a Pull Request."));
        return;
    }
    QStringList labels;
    for (const auto& task : tasks.value()) labels << QStringLiteral("%1 — %2")
        .arg(task.title, domain::planner::plannerStatusName(task.status));
    bool accepted = false;
    const QString choice = QInputDialog::getItem(this, tr("Link Pull Request to Planner task"),
        tr("Choose a task. Its existing Pull Request URL will be replaced."), labels, 0, false, &accepted);
    if (!accepted) return;
    const qsizetype selectedIndex = labels.indexOf(choice);
    if (selectedIndex < 0 || selectedIndex >= tasks.value().size()) return;
    const int index = static_cast<int>(selectedIndex);
    auto task = tasks.value().at(index);
    task.githubPullRequestUrl = selected->htmlUrl;
    const auto updated = m_planner->updateTask(std::move(task), tasks.value().at(index).revision);
    if (!updated) QMessageBox::warning(this, tr("Could not link Pull Request"), updated.error().userMessage);
    else {
        emit plannerTaskLinked(updated.value().id);
        QMessageBox::information(this, tr("Pull Request linked"), tr("The Planner task now opens this Pull Request."));
    }
}

std::optional<application::github::GitHubRepository> GitHubPullRequestsPanel::selectedRepository() const
{
    const QVariantMap data = m_repository->currentData().toMap();
    if (data.isEmpty()) return std::nullopt;
    return application::github::GitHubRepository{data.value(QStringLiteral("remote")).toString(),
        data.value(QStringLiteral("owner")).toString(), data.value(QStringLiteral("name")).toString()};
}

QString GitHubPullRequestsPanel::workspaceKey() const
{
    return m_workspaceId ? m_workspaceId->toString(QUuid::WithoutBraces) : QString{};
}

void GitHubPullRequestsPanel::notifyMonitorTarget()
{
    const auto repository = selectedRepository();
    if (!repository || workspaceKey().isEmpty()) {
        emit githubMonitorTargetChanged({}, {}, {}, {});
        return;
    }
    emit githubMonitorTargetChanged(workspaceKey(), repository->remoteName,
        repository->owner, repository->name);
}

} // namespace devhub::presentation::git
