#pragma once

#include "../../application/buildtest/BuildExecutionCoordinator.h"
#include "../../application/buildtest/BuildSystemDetector.h"
#include "../../application/buildtest/IBuildHistoryRepository.h"

#include <QWidget>

#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace devhub::application::workspaces { class WorkspaceContext; }
namespace devhub::application::files { class IExternalEditorLauncher; }

namespace devhub::presentation::buildtest {

class BuildTestPage final : public QWidget
{
    Q_OBJECT
public:
    BuildTestPage(application::buildtest::IBuildHistoryRepository& history,
                  application::buildtest::BuildExecutionCoordinator& execution,
                  application::workspaces::WorkspaceContext& workspaceContext,
                  application::files::IExternalEditorLauncher& editorLauncher,
                  QWidget* parent = nullptr);
    void setWorkspace(QString workspaceId, QString workspaceRoot);
    void runCurrentBuild();
    void runCurrentTests();
    void cancelCurrentRun();
    void openFailedTests();
    void runAssociatedProfile(const QString& profileId, const QString& taskId, bool testAction);
    void viewLastTaskResult(const QString& taskId);

signals:
    void navigationRequested(QString commandId);
    void profilesChanged();

private:
    void reloadProfiles();
    void reloadHistory();
    void updateControls();
    void runSelected(bool rebuild = false);
    void runProfile(application::buildtest::BuildProfile profile, bool rebuild = false, QString taskId = {});
    void manageProfiles();
    void createProfile();
    void rerunFailedTests();
    void runSelectedTest();
    void showRunDetails(const QString& runId);
    void setTestResults(const QVector<application::buildtest::TestResult>& tests);
    void setDiagnostics(const QVector<application::buildtest::Diagnostic>& diagnostics);
    void openDiagnostic(int row);
    [[nodiscard]] application::buildtest::BuildProfile selectedProfile() const;

    application::buildtest::IBuildHistoryRepository& m_history;
    application::buildtest::BuildExecutionCoordinator& m_execution;
    application::workspaces::WorkspaceContext& m_workspaceContext;
    application::files::IExternalEditorLauncher& m_editorLauncher;
    application::buildtest::BuildSystemDetector m_detector;
    std::optional<application::buildtest::DetectedProject> m_project;
    QVector<application::buildtest::BuildProfile> m_profiles;
    QStringList m_defaultProfileIds;
    QString m_workspaceId;
    QString m_workspaceRoot;
    QString m_activeRunId;
    QString m_displayedRunProfileId;
    bool m_paused = false;
    bool m_displayTruncated = false;
    QLabel* m_systemsLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_resultSummary = nullptr;
    QLabel* m_activeLabel = nullptr;
    QLabel* m_lastSuccessfulLabel = nullptr;
    QComboBox* m_profilesCombo = nullptr;
    QLineEdit* m_search = nullptr;
    QCheckBox* m_pause = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QTableWidget* m_tests = nullptr;
    QTableWidget* m_diagnostics = nullptr;
    QTableWidget* m_historyTable = nullptr;
    QPushButton* m_configureButton = nullptr;
    QPushButton* m_buildButton = nullptr;
    QPushButton* m_rebuildButton = nullptr;
    QPushButton* m_testButton = nullptr;
    QPushButton* m_rerunFailedButton = nullptr;
    QPushButton* m_runSelectedTestButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
};

} // namespace devhub::presentation::buildtest
