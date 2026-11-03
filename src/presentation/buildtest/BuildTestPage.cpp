#include "BuildTestPage.h"

#include "../../application/files/IExternalEditorLauncher.h"
#include "../../application/settings/SettingsService.h"
#include "../../application/workspaces/WorkspaceContext.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>

namespace devhub::presentation::buildtest {
namespace {
using namespace application::buildtest;

QString displayCommand(const CommandInvocation& invocation)
{
    QStringList lines{QObject::tr("Executable: %1").arg(invocation.executable), QObject::tr("Arguments:")};
    for (const QString& argument : invocation.arguments)
        lines.append(QStringLiteral("  • %1").arg(argument));
    return lines.join(QLatin1Char('\n'));
}

QString runTime(const QDateTime& value)
{
    return value.isValid() ? value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("—");
}

QString duration(qint64 milliseconds)
{
    if (milliseconds < 1000) return QStringLiteral("%1 ms").arg(milliseconds);
    return QStringLiteral("%1 s").arg(static_cast<double>(milliseconds) / 1000.0, 0, 'f', 1);
}

QTableWidget* createTable(int columns, QWidget* parent)
{
    auto* table = new QTableWidget(parent);
    table->setColumnCount(columns);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setAlternatingRowColors(true);
    return table;
}

} // namespace

BuildTestPage::BuildTestPage(application::buildtest::IBuildHistoryRepository& history,
                             application::buildtest::BuildExecutionCoordinator& execution,
                             application::workspaces::WorkspaceContext& workspaceContext,
                             application::files::IExternalEditorLauncher& editorLauncher,
                             QWidget* parent)
    : QWidget(parent), m_history(history), m_execution(execution), m_workspaceContext(workspaceContext),
      m_editorLauncher(editorLauncher)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 20, 24, 20);
    outer->setSpacing(10);
    auto* title = new QLabel(tr("Build & Test Center"), this);
    QFont titleFont = title->font(); titleFont.setPointSize(titleFont.pointSize() + 4); titleFont.setBold(true); title->setFont(titleFont);
    outer->addWidget(title);
    m_systemsLabel = new QLabel(tr("Select a workspace to detect build systems."), this);
    m_systemsLabel->setWordWrap(true);
    outer->addWidget(m_systemsLabel);
    m_lastSuccessfulLabel = new QLabel(tr("Last successful build: none in recent history"), this);
    m_lastSuccessfulLabel->setWordWrap(true);
    outer->addWidget(m_lastSuccessfulLabel);

    auto* profileBar = new QHBoxLayout();
    m_profilesCombo = new QComboBox(this);
    m_profilesCombo->setAccessibleName(tr("Build and test profile"));
    m_profilesCombo->setMinimumWidth(280);
    auto* manage = new QPushButton(tr("Manage Profiles…"), this);
    profileBar->addWidget(new QLabel(tr("Profile:"), this)); profileBar->addWidget(m_profilesCombo, 1); profileBar->addWidget(manage);
    outer->addLayout(profileBar);

    auto* controls = new QHBoxLayout();
    m_configureButton = new QPushButton(tr("Configure"), this);
    m_buildButton = new QPushButton(tr("Build"), this);
    m_rebuildButton = new QPushButton(tr("Rebuild"), this);
    m_testButton = new QPushButton(tr("Run Tests"), this);
    m_rerunFailedButton = new QPushButton(tr("Rerun Failed Tests"), this);
    m_runSelectedTestButton = new QPushButton(tr("Run Selected Test"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setEnabled(false);
    controls->addWidget(m_configureButton); controls->addWidget(m_buildButton); controls->addWidget(m_rebuildButton);
    controls->addWidget(m_testButton); controls->addStretch(); controls->addWidget(m_cancelButton);
    controls->insertWidget(controls->count() - 2, m_rerunFailedButton);
    controls->insertWidget(controls->count() - 2, m_runSelectedTestButton);
    outer->addLayout(controls);
    m_statusLabel = new QLabel(tr("Idle"), this);
    m_activeLabel = new QLabel(this); m_activeLabel->setWordWrap(true);
    outer->addWidget(m_statusLabel); outer->addWidget(m_activeLabel);

    auto* split = new QSplitter(Qt::Vertical, this);
    auto* outputPanel = new QWidget(split);
    auto* outputLayout = new QVBoxLayout(outputPanel); outputLayout->setContentsMargins(0, 0, 0, 0);
    auto* outputTools = new QHBoxLayout();
    m_search = new QLineEdit(outputPanel); m_search->setPlaceholderText(tr("Find in output")); m_search->setClearButtonEnabled(true);
    m_pause = new QCheckBox(tr("Pause display"), outputPanel);
    auto* copy = new QPushButton(tr("Copy Output"), outputPanel);
    auto* exportButton = new QPushButton(tr("Export…"), outputPanel);
    auto* clear = new QPushButton(tr("Clear View"), outputPanel);
    outputTools->addWidget(m_search, 1); outputTools->addWidget(m_pause); outputTools->addWidget(copy); outputTools->addWidget(exportButton); outputTools->addWidget(clear);
    outputLayout->addLayout(outputTools);
    m_output = new QPlainTextEdit(outputPanel); m_output->setReadOnly(true); m_output->setMaximumBlockCount(10000);
    m_output->setPlaceholderText(tr("Build and test output will appear here."));
    outputLayout->addWidget(m_output, 1); split->addWidget(outputPanel);

    auto* resultsPanel = new QWidget(split);
    auto* resultsLayout = new QVBoxLayout(resultsPanel); resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_resultSummary = new QLabel(tr("No test results in this session."), resultsPanel); resultsLayout->addWidget(m_resultSummary);
    m_tests = createTable(5, resultsPanel); m_tests->setHorizontalHeaderLabels({tr("Package"), tr("Test"), tr("Result"), tr("Duration"), tr("Details")});
    m_tests->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tests->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tests->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    resultsLayout->addWidget(m_tests, 1);
    m_diagnostics = createTable(5, resultsPanel);
    m_diagnostics->setHorizontalHeaderLabels({tr("Stage"), tr("Severity"), tr("File"), tr("Line"), tr("Message")});
    m_diagnostics->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_diagnostics->setMaximumHeight(150);
    resultsLayout->addWidget(m_diagnostics);
    split->addWidget(resultsPanel);
    split->setStretchFactor(0, 3); split->setStretchFactor(1, 2);
    outer->addWidget(split, 1);

    auto* historyTitle = new QLabel(tr("Recent Runs"), this); outer->addWidget(historyTitle);
    m_historyTable = createTable(8, this);
    m_historyTable->setMaximumHeight(160);
    m_historyTable->setHorizontalHeaderLabels({tr("Started"), tr("Profile"), tr("Runner"), tr("Status"), tr("Duration"), tr("Exit"), tr("Summary"), tr("Log reference")});
    m_historyTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    outer->addWidget(m_historyTable);

    connect(m_profilesCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &BuildTestPage::updateControls);
    connect(manage, &QPushButton::clicked, this, &BuildTestPage::manageProfiles);
    connect(m_configureButton, &QPushButton::clicked, this, [this] { runSelected(); });
    connect(m_buildButton, &QPushButton::clicked, this, [this] { runSelected(); });
    connect(m_rebuildButton, &QPushButton::clicked, this, [this] { runSelected(true); });
    connect(m_testButton, &QPushButton::clicked, this, [this] { runSelected(); });
    connect(m_rerunFailedButton, &QPushButton::clicked, this, &BuildTestPage::rerunFailedTests);
    connect(m_runSelectedTestButton, &QPushButton::clicked, this, &BuildTestPage::runSelectedTest);
    connect(m_tests, &QTableWidget::itemSelectionChanged, this, &BuildTestPage::updateControls);
    connect(m_cancelButton, &QPushButton::clicked, &m_execution, &application::buildtest::BuildExecutionCoordinator::cancel);
    connect(&m_execution, &application::buildtest::BuildExecutionCoordinator::phaseChanged,
            m_statusLabel, &QLabel::setText);
    connect(&m_execution, &application::buildtest::BuildExecutionCoordinator::outputAvailable, this,
            [this](const QString& runId, const QByteArray& bytes, bool standardError) {
        const auto activeWorkspace = m_workspaceContext.activeWorkspace();
        if (runId != m_activeRunId || m_paused || !activeWorkspace
            || activeWorkspace->id().toString() != m_workspaceId) return;
        QByteArray displayBytes = bytes;
        constexpr qsizetype maximumVisibleBytes = 2 * 1024 * 1024;
        if (displayBytes.size() > maximumVisibleBytes) displayBytes = displayBytes.right(maximumVisibleBytes);
        QString text = QString::fromUtf8(displayBytes);
        if (standardError) text.prepend(QStringLiteral("[stderr] "));
        QByteArray current = m_output->toPlainText().toUtf8();
        const qsizetype excess = current.size() + text.toUtf8().size() - maximumVisibleBytes;
        if (excess > 0) {
            current = current.mid(qMin<qsizetype>(excess, current.size()));
            const QString retained = QString::fromUtf8(current);
            text = (m_displayTruncated ? QString{} : tr("[Earlier output omitted to keep the view within 2 MiB]\n"))
                + retained + text;
            m_displayTruncated = true;
            m_output->clear();
        }
        m_output->moveCursor(QTextCursor::End);
        m_output->insertPlainText(text);
        m_output->moveCursor(QTextCursor::End);
    });
    connect(&m_execution, &application::buildtest::BuildExecutionCoordinator::runStartFailed,
            this, [this](const QString& message) { m_statusLabel->setText(message); });
    connect(&m_execution, &application::buildtest::BuildExecutionCoordinator::runFinished, this,
            [this](const BuildRun& run) {
        if (run.workspaceId != m_workspaceId) return;
        const QString status = run.testAction && run.tests.isEmpty() && run.status == RunStatus::Succeeded
            ? tr("Command succeeded · structured test results unavailable") : runStatusName(run.status);
        m_statusLabel->setText(tr("%1 · exit %2 · %3").arg(status).arg(run.exitCode).arg(duration(run.durationMilliseconds)));
        m_activeLabel->clear();
        m_displayedRunProfileId = run.profileId;
        setTestResults(run.tests);
        const QString testCounts = m_resultSummary->text();
        m_resultSummary->setText(run.testAction || isTestRunner(run.runner)
            ? run.summary + QStringLiteral("\n") + testCounts : run.summary);
        setDiagnostics(run.diagnostics); reloadHistory(); updateControls();
    });
    connect(m_pause, &QCheckBox::toggled, this, [this](bool paused) {
        m_paused = paused;
        if (!paused && !m_activeRunId.isEmpty()) {
            QString logReference;
            if (m_execution.running() && m_execution.currentRun().id == m_activeRunId)
                logReference = m_execution.currentRun().logReference;
            else {
                const auto runs = m_history.recentRuns(m_workspaceId, 50);
                if (runs) for (const BuildRun& run : runs.value())
                    if (run.id == m_activeRunId) { logReference = run.logReference; break; }
            }
            QFile file(logReference);
            if (file.open(QIODevice::ReadOnly)) {
                file.seek(qMax<qint64>(0, file.size() - 2 * 1024 * 1024));
                const bool truncated = file.size() > 2 * 1024 * 1024;
                QString text = QString::fromUtf8(file.read(2 * 1024 * 1024));
                if (truncated) text.prepend(tr("[Earlier output omitted to keep the view within 2 MiB]\n"));
                m_displayTruncated = truncated;
                m_output->setPlainText(text);
            }
        }
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        if (!m_search->text().isEmpty() && !m_output->find(m_search->text())) {
            QTextCursor cursor = m_output->textCursor(); cursor.movePosition(QTextCursor::Start); m_output->setTextCursor(cursor);
            (void)m_output->find(m_search->text());
        }
    });
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_output->toPlainText()); });
    connect(clear, &QPushButton::clicked, this, [this] { m_output->clear(); m_displayTruncated = false; });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export build output"), {}, tr("Text files (*.txt);;All files (*)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(m_output->toPlainText().toUtf8()) < 0)
            QMessageBox::warning(this, tr("Export failed"), file.errorString());
    });
    connect(m_historyTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QString id = m_historyTable->item(row, 0)->data(Qt::UserRole).toString();
        showRunDetails(id);
    });
    connect(m_diagnostics, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { openDiagnostic(row); });
    updateControls();
}

void BuildTestPage::setWorkspace(QString workspaceId, QString workspaceRoot)
{
    m_workspaceId = std::move(workspaceId); m_workspaceRoot = std::move(workspaceRoot);
    m_activeRunId.clear(); m_displayedRunProfileId.clear();
    m_project.reset(); m_profiles.clear(); m_defaultProfileIds.clear(); m_displayTruncated = false;
    m_tests->setRowCount(0); m_diagnostics->setRowCount(0); m_output->clear();
    m_lastSuccessfulLabel->setText(tr("Last successful build: none in recent history"));
    if (m_workspaceId.isEmpty() || m_workspaceRoot.isEmpty()) {
        m_systemsLabel->setText(tr("Select a workspace to detect build systems."));
        m_profilesCombo->clear(); m_historyTable->setRowCount(0); updateControls(); return;
    }
    auto detected = m_detector.detect(m_workspaceId, m_workspaceRoot);
    if (!detected) {
        m_systemsLabel->setText(detected.error().userMessage);
        m_profilesCombo->clear(); m_historyTable->setRowCount(0); updateControls(); return;
    }
    m_project = std::move(detected).value();
    m_systemsLabel->setText(tr("Workspace: %1\nDetected: %2\nCMake configure presets: %3 · build presets: %4 · test presets: %5\nNode scripts: %6")
        .arg(m_workspaceRoot, m_project->buildSystems.isEmpty() ? tr("no supported build system") : m_project->buildSystems.join(QStringLiteral(", ")),
             m_project->configurePresets.join(QStringLiteral(", ")), m_project->buildPresets.join(QStringLiteral(", ")),
             m_project->testPresets.join(QStringLiteral(", ")), m_project->nodeScripts.join(QStringLiteral(", "))));
    if (!m_project->warnings.isEmpty()) m_systemsLabel->setText(m_systemsLabel->text() + QStringLiteral("\n") + m_project->warnings.join(QStringLiteral("\n")));
    auto defaults = m_detector.defaultProfiles(*m_project);
    for (const BuildProfile& profile : defaults) {
        m_defaultProfileIds.append(profile.id);
        auto saved = m_history.saveProfile(profile);
        Q_UNUSED(saved);
    }
    reloadProfiles(); reloadHistory(); updateControls();
}

void BuildTestPage::reloadProfiles()
{
    m_profiles.clear(); m_profilesCombo->blockSignals(true); m_profilesCombo->clear();
    if (!m_workspaceId.isEmpty()) {
        auto rows = m_history.profiles(m_workspaceId);
        if (rows) m_profiles = std::move(rows).value();
    }
    for (const BuildProfile& profile : m_profiles)
        m_profilesCombo->addItem(QStringLiteral("%1  ·  %2%3").arg(profile.name, runnerName(profile.runner),
            profile.trusted ? QString{} : tr("  (approval required)")), profile.id);
    m_profilesCombo->blockSignals(false);
    if (m_profilesCombo->count() > 0) m_profilesCombo->setCurrentIndex(0);
    updateControls();
    emit profilesChanged();
}

void BuildTestPage::reloadHistory()
{
    m_historyTable->setRowCount(0);
    if (m_workspaceId.isEmpty()) return;
    auto rows = m_history.recentRuns(m_workspaceId, 30);
    if (!rows) { m_statusLabel->setText(rows.error().userMessage); return; }
    m_lastSuccessfulLabel->setText(tr("Last successful build: none in recent history"));
    for (const BuildRun& run : rows.value()) {
        const int row = m_historyTable->rowCount(); m_historyTable->insertRow(row);
        auto* time = new QTableWidgetItem(runTime(run.startedAtUtc)); time->setData(Qt::UserRole, run.id);
        m_historyTable->setItem(row, 0, time);
        m_historyTable->setItem(row, 1, new QTableWidgetItem(run.profileName));
        m_historyTable->setItem(row, 2, new QTableWidgetItem(runnerName(run.runner)));
        m_historyTable->setItem(row, 3, new QTableWidgetItem(runStatusName(run.status)));
        m_historyTable->setItem(row, 4, new QTableWidgetItem(duration(run.durationMilliseconds)));
        m_historyTable->setItem(row, 5, new QTableWidgetItem(QString::number(run.exitCode)));
        m_historyTable->setItem(row, 6, new QTableWidgetItem(run.summary));
        auto* log = new QTableWidgetItem(QFileInfo(run.logReference).fileName());
        log->setToolTip(run.logReference);
        m_historyTable->setItem(row, 7, log);
        if (m_lastSuccessfulLabel->text() == tr("Last successful build: none in recent history")
            && !run.testAction && run.status == RunStatus::Succeeded)
            m_lastSuccessfulLabel->setText(tr("Last successful build: %1 · %2 · %3")
                .arg(run.profileName, runTime(run.startedAtUtc), run.summary));
    }
}

BuildProfile BuildTestPage::selectedProfile() const
{
    const QString id = m_profilesCombo->currentData().toString();
    for (const BuildProfile& profile : m_profiles) if (profile.id == id) return profile;
    return {};
}

void BuildTestPage::updateControls()
{
    const BuildProfile profile = selectedProfile();
    const bool hasProfile = !profile.id.isEmpty();
    const bool busy = m_execution.running();
    const bool activeWorkspace = !m_workspaceId.isEmpty() && m_workspaceContext.activeWorkspace()
        && m_workspaceContext.activeWorkspace()->id().toString() == m_workspaceId;
    m_configureButton->setEnabled(!busy && activeWorkspace && hasProfile && profile.runner == RunnerType::CMakeConfigure);
    m_buildButton->setEnabled(!busy && activeWorkspace && hasProfile && !profile.testAction
        && profile.runner != RunnerType::CMakeConfigure);
    m_rebuildButton->setEnabled(!busy && activeWorkspace && hasProfile && profile.runner == RunnerType::CMakeBuild);
    m_testButton->setEnabled(!busy && activeWorkspace && hasProfile && profile.testAction);
    m_rerunFailedButton->setEnabled(!busy && activeWorkspace);
    const auto displayedProfile = std::find_if(m_profiles.cbegin(), m_profiles.cend(), [this](const BuildProfile& item) {
        return item.id == m_displayedRunProfileId && item.testAction
            && (item.runner == RunnerType::CTest || item.runner == RunnerType::GoTest || item.runner == RunnerType::Pytest);
    });
    m_runSelectedTestButton->setEnabled(!busy && activeWorkspace && displayedProfile != m_profiles.cend()
        && m_tests->currentRow() >= 0);
    m_cancelButton->setEnabled(busy && m_execution.activeWorkspaceId() == m_workspaceId);
    m_profilesCombo->setEnabled(!busy);
    if (!activeWorkspace) m_activeLabel->setText(m_workspaceId.isEmpty() ? tr("Select a workspace before starting a build or test run.")
        : tr("The active workspace changed. Select this workspace to start an operation here."));
}

void BuildTestPage::runSelected(bool rebuild)
{
    runProfile(selectedProfile(), rebuild);
}

void BuildTestPage::runProfile(BuildProfile profile, bool rebuild, QString taskId)
{
    if (profile.id.isEmpty() || !m_project) return;
    auto resolved = m_execution.resolve(profile, m_workspaceRoot, rebuild);
    if (!resolved) { QMessageBox::warning(this, tr("Cannot start operation"), resolved.error().userMessage); return; }
    bool trustedNow = profile.trusted;
    if (!profile.trusted) {
        QDialog approval(this); approval.setWindowTitle(tr("Review project command"));
        auto* layout = new QVBoxLayout(&approval);
        auto* warning = new QLabel(tr("This profile runs project-provided code. Review the executable, arguments, and working directory before continuing."), &approval);
        warning->setWordWrap(true); layout->addWidget(warning);
        auto* command = new QPlainTextEdit(&approval); command->setReadOnly(true); command->setMaximumHeight(100);
        command->setPlainText(displayCommand(resolved.value()) + QStringLiteral("\n\nWorking directory: ") + resolved.value().workingDirectory);
        layout->addWidget(command);
        auto* trust = new QCheckBox(tr("Trust this profile for future runs in this workspace"), &approval); layout->addWidget(trust);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::Cancel, &approval);
        buttons->button(QDialogButtonBox::Yes)->setText(tr("Run Once")); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &approval, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &approval, &QDialog::reject);
        if (approval.exec() != QDialog::Accepted) return;
        trustedNow = true;
        if (trust->isChecked()) {
            auto result = m_history.setProfileTrusted(m_workspaceId, profile.id, true);
            if (!result) { QMessageBox::warning(this, tr("Could not save profile trust"), result.error().userMessage); return; }
            for (BuildProfile& current : m_profiles) if (current.id == profile.id) current.trusted = true;
            const int index = m_profilesCombo->currentIndex();
            m_profilesCombo->setItemText(index, QStringLiteral("%1  ·  %2").arg(profile.name, runnerName(profile.runner)));
        }
    }
    m_activeRunId.clear(); m_displayTruncated = false; m_output->clear(); m_tests->setRowCount(0); m_diagnostics->setRowCount(0);
    auto started = m_execution.start(profile, m_workspaceRoot, rebuild, trustedNow, std::move(taskId));
    if (!started) { m_statusLabel->setText(started.error().userMessage); return; }
    m_activeRunId = started.value();
    m_activeLabel->setText(tr("Running %1 · %2").arg(profile.name, displayCommand(resolved.value())));
    updateControls();
}

void BuildTestPage::runAssociatedProfile(const QString& profileId, const QString& taskId, bool testAction)
{
    if (m_workspaceId.isEmpty() || !m_workspaceContext.activeWorkspace()
        || m_workspaceContext.activeWorkspace()->id().toString() != m_workspaceId) return;
    for (const BuildProfile& profile : m_profiles) {
        if (profile.id != profileId || profile.testAction != testAction) continue;
        const int index = m_profilesCombo->findData(profileId);
        if (index >= 0) m_profilesCombo->setCurrentIndex(index);
        runProfile(profile, false, taskId);
        return;
    }
    m_statusLabel->setText(tr("The Planner-linked profile is no longer available in this workspace."));
}

void BuildTestPage::viewLastTaskResult(const QString& taskId)
{
    const auto runs = m_history.recentRuns(m_workspaceId, 50);
    if (!runs) { m_statusLabel->setText(runs.error().userMessage); return; }
    for (const BuildRun& run : runs.value()) {
        if (run.taskId == taskId && run.testAction) {
            showRunDetails(run.id);
            return;
        }
    }
    m_statusLabel->setText(tr("No test result is linked to this Planner task yet."));
}

void BuildTestPage::rerunFailedTests()
{
    if (m_workspaceId.isEmpty() || !m_project) return;
    const auto runs = m_history.recentRuns(m_workspaceId, 50);
    if (!runs) { m_statusLabel->setText(runs.error().userMessage); return; }
    for (const BuildRun& run : runs.value()) {
        if (!run.testAction || run.failedTests <= 0) continue;
        auto results = m_history.testResults(m_workspaceId, run.id);
        if (!results) { m_statusLabel->setText(results.error().userMessage); return; }
        QStringList failedNames;
        for (const TestResult& result : results.value())
            if (result.status == QStringLiteral("failed") && !result.name.isEmpty())
                failedNames.append(result.name);
        const auto profileIt = std::find_if(m_profiles.cbegin(), m_profiles.cend(), [&run](const BuildProfile& profile) {
            return profile.id == run.profileId && profile.testAction;
        });
        if (profileIt == m_profiles.cend()) continue;
        BuildProfile retry = *profileIt;
        if (retry.runner == RunnerType::CTest || retry.runner == RunnerType::GoTest) {
            if (failedNames.isEmpty()) continue;
            QStringList expressions;
            for (const QString& original : failedNames) {
                const QString name = retry.runner == RunnerType::GoTest
                    ? original.section(QLatin1Char('/'), 0, 0) : original;
                const QString expression = QRegularExpression::escape(name);
                if (!expressions.contains(expression)) expressions.append(expression);
            }
            retry.testFilter = QStringLiteral("^(%1)$").arg(expressions.join(QLatin1Char('|')));
        } else if (retry.runner == RunnerType::Pytest) {
            retry.arguments.append(QStringLiteral("--lf"));
        } else {
            continue;
        }
        const int index = m_profilesCombo->findData(retry.id);
        if (index >= 0) m_profilesCombo->setCurrentIndex(index);
        runProfile(std::move(retry), false, run.taskId);
        return;
    }
    m_statusLabel->setText(tr("No recent failed test run has a supported rerun filter."));
}

void BuildTestPage::runSelectedTest()
{
    const int row = m_tests->currentRow();
    if (row < 0 || m_displayedRunProfileId.isEmpty() || !m_tests->item(row, 1)) return;
    const auto profileIt = std::find_if(m_profiles.cbegin(), m_profiles.cend(), [this](const BuildProfile& profile) {
        return profile.id == m_displayedRunProfileId && profile.testAction;
    });
    if (profileIt == m_profiles.cend()) { m_statusLabel->setText(tr("The selected test profile is no longer available.")); return; }
    const QString name = m_tests->item(row, 1)->text();
    if (name.isEmpty()) return;
    BuildProfile retry = *profileIt;
    if (retry.runner == RunnerType::CTest || retry.runner == RunnerType::GoTest) {
        const QString selected = retry.runner == RunnerType::GoTest ? name.section(QLatin1Char('/'), 0, 0) : name;
        retry.testFilter = QStringLiteral("^(%1)$").arg(QRegularExpression::escape(selected));
    }
    else if (retry.runner == RunnerType::Pytest) {
        QString keyword = name;
        keyword.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QString{});
        if (keyword.isEmpty()) { m_statusLabel->setText(tr("This pytest item cannot be selected by name.")); return; }
        retry.arguments.append(QStringLiteral("-k")); retry.arguments.append(keyword);
    } else {
        m_statusLabel->setText(tr("Selected test execution is not available for this runner.")); return;
    }
    QString taskId;
    const auto runs = m_history.recentRuns(m_workspaceId, 50);
    if (runs) for (const BuildRun& run : runs.value())
        if (run.id == m_activeRunId) { taskId = run.taskId; break; }
    runProfile(std::move(retry), false, taskId);
}

void BuildTestPage::manageProfiles()
{
    QDialog dialog(this); dialog.setWindowTitle(tr("Manage Build & Test Profiles"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QComboBox(&dialog);
    for (const BuildProfile& profile : m_profiles) list->addItem(QStringLiteral("%1 · %2").arg(profile.name, runnerName(profile.runner)), profile.id);
    layout->addWidget(list);
    auto* buttons = new QHBoxLayout(); auto* add = new QPushButton(tr("Add Profile…"), &dialog);
    auto* remove = new QPushButton(tr("Remove Selected"), &dialog); buttons->addWidget(add); buttons->addWidget(remove); buttons->addStretch();
    layout->addLayout(buttons);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(add, &QPushButton::clicked, &dialog, [this, &dialog] { dialog.hide(); createProfile(); dialog.show(); });
    connect(remove, &QPushButton::clicked, &dialog, [this, list, &dialog] {
        const QString id = list->currentData().toString();
        if (id.isEmpty() || m_defaultProfileIds.contains(id)) {
            QMessageBox::information(&dialog, tr("Built-in profile"), tr("Detected runner profiles are managed by the project configuration.")); return;
        }
        if (QMessageBox::question(&dialog, tr("Remove profile"), tr("Remove this workspace profile? Existing run history will be kept.")) != QMessageBox::Yes) return;
        auto removed = m_history.removeProfile(m_workspaceId, id);
        if (!removed) QMessageBox::warning(&dialog, tr("Could not remove profile"), removed.error().userMessage);
        reloadProfiles();
        list->clear(); for (const BuildProfile& item : m_profiles) list->addItem(item.name + QStringLiteral(" · ") + runnerName(item.runner), item.id);
    });
    (void)dialog.exec();
}

void BuildTestPage::createProfile()
{
    if (!m_project) return;
    QDialog dialog(this); dialog.setWindowTitle(tr("Add Build & Test Profile"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(&dialog);
    auto* runner = new QComboBox(&dialog);
    runner->addItem(tr("Custom command"), static_cast<int>(RunnerType::Custom));
    if (m_project->hasNode) runner->addItem(tr("Node.js script"), static_cast<int>(RunnerType::NpmScript));
    if (m_project->hasMake) runner->addItem(tr("Make target"), static_cast<int>(RunnerType::MakeTarget));
    if (m_project->hasGoModule || m_project->hasGoWorkspace) runner->addItem(tr("Go tests"), static_cast<int>(RunnerType::GoTest));
    auto* program = new QLineEdit(&dialog); program->setPlaceholderText(tr("Executable path or command name"));
    auto* script = new QComboBox(&dialog); script->addItems(m_project->nodeScripts);
    auto* target = new QLineEdit(&dialog); target->setPlaceholderText(tr("One target name"));
    auto* arguments = new QLineEdit(&dialog); arguments->setPlaceholderText(tr("Arguments, passed separately (no shell parsing)"));
    auto* working = new QLineEdit(m_workspaceRoot, &dialog);
    auto* workingBrowse = new QPushButton(tr("Browse…"), &dialog);
    auto* workingRow = new QHBoxLayout(); workingRow->addWidget(working, 1); workingRow->addWidget(workingBrowse);
    auto* testAction = new QCheckBox(tr("This profile runs automated tests"), &dialog);
    auto* goRace = new QCheckBox(tr("Enable Go race detector (requires CGO support)"), &dialog);
    auto* goArguments = new QLineEdit(QStringLiteral("./..."), &dialog);
    auto* timeout = new QSpinBox(&dialog); timeout->setRange(1, 86400); timeout->setValue(1800); timeout->setSuffix(tr(" seconds"));
    form->addRow(tr("Profile name"), name); form->addRow(tr("Runner"), runner); form->addRow(tr("Executable"), program);
    form->addRow(tr("Node script"), script); form->addRow(tr("Make target"), target); form->addRow(tr("Additional arguments"), arguments);
    form->addRow(tr("Go packages / flags"), goArguments); form->addRow(goRace);
    form->addRow(tr("Working directory"), workingRow); form->addRow(testAction); form->addRow(tr("Timeout"), timeout);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog); form->addRow(buttons);
    auto updateRunnerFields = [runner, program, script, target, arguments, goArguments, goRace, testAction] {
        const auto type = static_cast<RunnerType>(runner->currentData().toInt());
        program->setVisible(type == RunnerType::Custom);
        script->setVisible(type == RunnerType::NpmScript);
        target->setVisible(type == RunnerType::MakeTarget);
        arguments->setEnabled(type == RunnerType::Custom || type == RunnerType::NpmScript || type == RunnerType::MakeTarget);
        goArguments->setVisible(type == RunnerType::GoTest);
        goRace->setVisible(type == RunnerType::GoTest);
        goRace->setEnabled(type == RunnerType::GoTest);
        testAction->setEnabled(type == RunnerType::Custom || type == RunnerType::NpmScript || type == RunnerType::MakeTarget);
        if (type == RunnerType::GoTest) testAction->setChecked(true);
        if (type == RunnerType::NpmScript) testAction->setChecked(false);
    };
    connect(runner, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [updateRunnerFields] { updateRunnerFields(); });
    connect(workingBrowse, &QPushButton::clicked, &dialog, [&dialog, working] {
        const QString path = QFileDialog::getExistingDirectory(&dialog, QObject::tr("Choose working directory"), working->text());
        if (!path.isEmpty()) working->setText(path);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, name, runner, script, target, program, arguments, working, testAction] {
        if (name->text().trimmed().isEmpty()) { QMessageBox::warning(&dialog, QObject::tr("Profile name required"), QObject::tr("Enter a profile name.")); return; }
        const auto type = static_cast<RunnerType>(runner->currentData().toInt());
        if (type == RunnerType::NpmScript && script->currentText().isEmpty()) { QMessageBox::warning(&dialog, QObject::tr("Script required"), QObject::tr("Select an existing npm script.")); return; }
        if (type == RunnerType::MakeTarget && target->text().trimmed().isEmpty()) { QMessageBox::warning(&dialog, QObject::tr("Target required"), QObject::tr("Enter one Make target.")); return; }
        if (type == RunnerType::Custom && program->text().trimmed().isEmpty()) { QMessageBox::warning(&dialog, QObject::tr("Executable required"), QObject::tr("Enter an executable.")); return; }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateRunnerFields();
    if (dialog.exec() != QDialog::Accepted) return;
    BuildProfile profile;
    profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces); profile.workspaceId = m_workspaceId;
    profile.name = name->text().trimmed(); profile.runner = static_cast<RunnerType>(runner->currentData().toInt());
    profile.workingDirectory = working->text().trimmed(); profile.program = program->text().trimmed();
    profile.target = profile.runner == RunnerType::NpmScript ? script->currentText() : target->text().trimmed();
    profile.arguments = profile.runner == RunnerType::GoTest
        ? QProcess::splitCommand(goArguments->text()) : QProcess::splitCommand(arguments->text());
    profile.timeoutSeconds = timeout->value();
    profile.testAction = testAction->isChecked();
    if (profile.runner == RunnerType::GoTest) {
        if (goRace->isChecked()) profile.arguments.prepend(QStringLiteral("-race"));
        profile.testAction = true;
    }
    auto saved = m_history.saveProfile(profile);
    if (!saved) { QMessageBox::warning(this, tr("Could not save profile"), saved.error().userMessage); return; }
    reloadProfiles();
}

void BuildTestPage::setTestResults(const QVector<application::buildtest::TestResult>& tests)
{
    m_tests->setRowCount(0);
    int passed = 0, failed = 0, skipped = 0;
    for (const TestResult& test : tests) {
        const int row = m_tests->rowCount(); m_tests->insertRow(row);
        m_tests->setItem(row, 0, new QTableWidgetItem(test.packageName)); m_tests->setItem(row, 1, new QTableWidgetItem(test.name));
        m_tests->setItem(row, 2, new QTableWidgetItem(test.status)); m_tests->setItem(row, 3, new QTableWidgetItem(duration(test.durationMilliseconds)));
        m_tests->setItem(row, 4, new QTableWidgetItem(test.details.left(500)));
        if (test.status == QStringLiteral("passed")) ++passed;
        else if (test.status == QStringLiteral("failed")) ++failed;
        else if (test.status == QStringLiteral("skipped")) ++skipped;
    }
    m_resultSummary->setText(tests.isEmpty() ? tr("No structured per-test results were available for the selected run.")
        : tr("%1 total · %2 passed · %3 failed · %4 skipped").arg(tests.size()).arg(passed).arg(failed).arg(skipped));
}

void BuildTestPage::setDiagnostics(const QVector<application::buildtest::Diagnostic>& diagnostics)
{
    m_diagnostics->setRowCount(0);
    for (const Diagnostic& diagnostic : diagnostics) {
        const int row = m_diagnostics->rowCount(); m_diagnostics->insertRow(row);
        m_diagnostics->setItem(row, 0, new QTableWidgetItem(diagnostic.stage));
        m_diagnostics->setItem(row, 1, new QTableWidgetItem(diagnostic.severity));
        auto* file = new QTableWidgetItem(diagnostic.filePath); file->setData(Qt::UserRole, diagnostic.filePath);
        m_diagnostics->setItem(row, 2, file); m_diagnostics->setItem(row, 3, new QTableWidgetItem(QString::number(diagnostic.line)));
        m_diagnostics->setItem(row, 4, new QTableWidgetItem(diagnostic.message));
    }
}

void BuildTestPage::showRunDetails(const QString& runId)
{
    auto results = m_history.testResults(m_workspaceId, runId);
    if (results) setTestResults(results.value());
    auto runs = m_history.recentRuns(m_workspaceId, 30);
    if (!runs) return;
    for (const BuildRun& run : runs.value()) {
        if (run.id != runId) continue;
        m_activeRunId = runId;
        m_displayedRunProfileId = run.profileId;
        if (run.testAction || isTestRunner(run.runner))
            m_resultSummary->setText(run.summary + QStringLiteral("\n") + m_resultSummary->text());
        else m_resultSummary->setText(run.summary);
        if (!run.logReference.isEmpty() && QFileInfo::exists(run.logReference)) {
            QFile file(run.logReference);
            if (file.open(QIODevice::ReadOnly)) {
                const bool truncated = file.size() > 2 * 1024 * 1024;
                file.seek(qMax<qint64>(0, file.size() - 2 * 1024 * 1024));
                QString text = QString::fromUtf8(file.read(2 * 1024 * 1024));
                if (truncated) text.prepend(tr("[Earlier output omitted to keep the view within 2 MiB]\n"));
                m_displayTruncated = truncated;
                m_output->setPlainText(text);
                file.close();
            }
        }
        break;
    }
}

void BuildTestPage::openDiagnostic(int row)
{
    if (row < 0 || row >= m_diagnostics->rowCount()) return;
    const QString reported = m_diagnostics->item(row, 2)->data(Qt::UserRole).toString();
    QFileInfo candidate(reported.isEmpty() ? m_diagnostics->item(row, 2)->text() : reported);
    const QString absolute = candidate.isAbsolute() ? candidate.absoluteFilePath() : QDir(m_workspaceRoot).filePath(candidate.filePath());
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    const QString root = QFileInfo(m_workspaceRoot).canonicalFilePath();
    if (canonical.isEmpty() || root.isEmpty()) { QMessageBox::information(this, tr("File unavailable"), tr("The diagnostic file no longer exists.")); return; }
    const QString prefix = root.endsWith(QDir::separator()) ? root : root + QDir::separator();
#ifdef Q_OS_WIN
    const bool inside = canonical.compare(root, Qt::CaseInsensitive) == 0 || canonical.startsWith(prefix, Qt::CaseInsensitive);
#else
    const bool inside = canonical == root || canonical.startsWith(prefix);
#endif
    if (!inside || !QFileInfo(canonical).isFile()) { QMessageBox::warning(this, tr("Editor blocked"), tr("The diagnostic path is outside the active workspace.")); return; }
    QString editor = QSettings().value(QStringLiteral("files/editorProgram")).toString();
    if (editor.isEmpty() || !QFileInfo(editor).isFile()) {
        editor = QFileDialog::getOpenFileName(this, tr("Choose IDE or editor executable"), {}, tr("Applications (*.exe);;All files (*)"));
        if (editor.isEmpty()) return;
        QSettings().setValue(QStringLiteral("files/editorProgram"), editor);
    }
    auto launched = m_editorLauncher.launch(editor, canonical);
    if (!launched) QMessageBox::warning(this, tr("Could not open file"), launched.error().userMessage);
}

void BuildTestPage::openFailedTests()
{
    for (int row = 0; row < m_historyTable->rowCount(); ++row) {
        if (m_historyTable->item(row, 3)->text() == runStatusName(RunStatus::Failed)) {
            showRunDetails(m_historyTable->item(row, 0)->data(Qt::UserRole).toString());
            return;
        }
    }
    m_statusLabel->setText(tr("No failed run is available in recent history."));
}

void BuildTestPage::runCurrentBuild()
{
    if (selectedProfile().id.isEmpty() || selectedProfile().testAction) {
        for (const BuildProfile& profile : m_profiles) {
            if (!profile.testAction && profile.runner != RunnerType::CMakeConfigure) {
                const int index = m_profilesCombo->findData(profile.id);
                if (index >= 0) m_profilesCombo->setCurrentIndex(index);
                break;
            }
        }
    }
    const BuildProfile profile = selectedProfile();
    if (profile.id.isEmpty() || profile.testAction || profile.runner == RunnerType::CMakeConfigure) {
        m_statusLabel->setText(tr("No build profile is configured. Use Manage Profiles to add one.")); return;
    }
    runSelected();
}

void BuildTestPage::runCurrentTests()
{
    if (!selectedProfile().testAction) {
        for (const BuildProfile& profile : m_profiles) {
            if (profile.testAction) {
                const int index = m_profilesCombo->findData(profile.id);
                if (index >= 0) m_profilesCombo->setCurrentIndex(index);
                break;
            }
        }
    }
    const BuildProfile profile = selectedProfile();
    if (profile.id.isEmpty() || !profile.testAction) {
        m_statusLabel->setText(tr("No test profile is configured. Use Manage Profiles to add one.")); return;
    }
    runSelected();
}

void BuildTestPage::cancelCurrentRun()
{
    m_execution.cancel();
}

} // namespace devhub::presentation::buildtest
