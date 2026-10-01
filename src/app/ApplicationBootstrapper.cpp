#include "ApplicationBootstrapper.h"

#include "RecoveryModeWindow.h"
#include "StartupFailure.h"
#include "../application/accounts/IAccountRepository.h"
#include "../application/authentication/AuthenticationProductPolicy.h"
#include "../application/authentication/AuthenticationService.h"
#include "../application/detection/WorkspaceDetectorRegistry.h"
#include "../application/detection/WorkspaceScanService.h"
#include "../application/detection/WorkspaceScanner.h"
#include "../application/authentication/InactivityMonitor.h"
#include "../application/authentication/SessionController.h"
#include "../application/workspaces/WorkspaceContext.h"
#include "../application/workspaces/WorkspaceManager.h"
#include "../application/workspaces/WorkspaceRelocationService.h"
#include "../application/workspaces/WorkspaceStateStore.h"
#include "../application/workspaces/WorkspaceValidation.h"
#include "../application/authentication/SystemMonotonicClock.h"
#include "../application/events/ApplicationEventHub.h"
#include "../application/lifecycle/ApplicationLifecycle.h"
#include "../application/lifecycle/ShutdownCoordinator.h"
#include "../application/notifications/NotificationService.h"
#include "../application/tasks/TaskScheduler.h"
#include "../infrastructure/filesystem/ApplicationPaths.h"
#include "../infrastructure/database/InternalDatabaseManager.h"
#include "../infrastructure/account/SodiumPasswordHasher.h"
#include "../infrastructure/account/SqlAccountRepository.h"
#include "../infrastructure/filesystem/LocalUserDirectoryProvisioner.h"
#include "../infrastructure/filesystem/LocalProfileResetter.h"
#include "../infrastructure/workspace/SqlWorkspaceRepository.h"
#include "../infrastructure/services/SqlServiceDefinitionRepository.h"
#include "../infrastructure/ports/WindowsPortProvider.h"
#include "../infrastructure/ports/WindowsProcessInspector.h"
#include "../infrastructure/logs/SqlLogSourceRepository.h"
#include "../infrastructure/database/SqlDatabaseReferenceRepository.h"
#include "../infrastructure/database/QtSqlConnectionFactory.h"
#include "../infrastructure/notes/NotesRepository.h"
#include "../infrastructure/files/WindowsTrashService.h"
#include "../infrastructure/files/QProcessExternalEditorLauncher.h"
#include "../application/services/ManagedProcess.h"
#include "../application/services/ServiceCoordinator.h"
#include "../application/ports/PortMonitorService.h"
#include "../infrastructure/workspace/WindowsTerminalLauncher.h"
#include "../infrastructure/settings/SqlSettingsRepository.h"
#include "../infrastructure/notifications/SqlNotificationRepository.h"
#include "../infrastructure/logging/ApplicationLogManager.h"
#include "../application/support/SupportBundleBuilder.h"
#include "../application/settings/SettingsService.h"
#include "../infrastructure/detection/SqlDetectionResultRepository.h"
#include "../presentation/authentication/AuthenticationWindow.h"
#include "../presentation/shell/MainWindow.h"
#include "../presentation/theme/ThemeManager.h"
#include "../presentation/theme/ThemePalette.h"
#include "../shared/security/RedactionService.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

#include <utility>
#include <algorithm>

namespace devhub::app {
namespace {

class StartupCrashMarker final
{
public:
    explicit StartupCrashMarker(const QString& localDataDirectory)
        : m_path(QDir(localDataDirectory).filePath(QStringLiteral("startup-in-progress"))),
          m_previousRunWasUnclean(QFileInfo::exists(m_path))
    {
        QFile marker(m_path);
        if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qWarning().noquote() << QStringLiteral("Could not create startup marker: %1").arg(marker.errorString());
            return;
        }
        marker.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8());
        marker.write("\n");
        m_written = marker.flush();
    }

    ~StartupCrashMarker() { markClean(); }

    [[nodiscard]] bool previousRunWasUnclean() const noexcept { return m_previousRunWasUnclean; }

    void markClean()
    {
        if (m_written && QFile::exists(m_path) && !QFile::remove(m_path))
            qWarning().noquote() << QStringLiteral("Could not remove startup marker: %1").arg(m_path);
        m_written = false;
    }

private:
    QString m_path;
    bool m_previousRunWasUnclean = false;
    bool m_written = false;
};

} // namespace

ApplicationBootstrapper::ApplicationBootstrapper(QApplication& application,
                                                 shared::time::IApplicationClock& clock,
                                                 shared::identifiers::IIdGenerator& idGenerator)
    : m_application(application),
      m_clock(clock),
      m_idGenerator(idGenerator)
{
}

int ApplicationBootstrapper::run()
{
    m_application.setOrganizationName(QStringLiteral(DEVHUB_ORGANIZATION));
    m_application.setApplicationName(QStringLiteral(DEVHUB_APPLICATION_NAME));
    m_application.setApplicationVersion(QStringLiteral(DEVHUB_VERSION));
    m_application.setQuitOnLastWindowClosed(true);
    const auto arguments = m_application.arguments();
    if (std::any_of(arguments.cbegin(), arguments.cend(), [](const QString& argument) {
            return argument.startsWith(QStringLiteral("--capture-auth="));
        })) {
        QStandardPaths::setTestModeEnabled(true);
    }

    QElapsedTimer startupTimer;
    startupTimer.start();
    bool recoveredFromUncleanExit = false;

    for (;;) {
        auto paths = infrastructure::filesystem::ApplicationPaths::resolveAndPrepare();
        if (!paths) {
            logFatalStartupError(paths.error());
            StartupFailure failure{paths.error(),
                                   {StartupRecoveryAction::Retry, StartupRecoveryAction::Quit},
                                   {},
                                   {}};
            RecoveryModeWindow recovery(std::move(failure));
            recovery.exec();
            if (recovery.selectedAction() == StartupRecoveryAction::Retry) {
                continue;
            }
            return 1;
        }

        StartupCrashMarker crashMarker(paths.value().localDataDirectory);
        recoveredFromUncleanExit = recoveredFromUncleanExit || crashMarker.previousRunWasUnclean();

        infrastructure::logging::ApplicationLogManager logManager(paths.value().logDirectory);

        auto sodiumResult = infrastructure::account::SodiumPasswordHasher::initialize();
        if (!sodiumResult) {
            logFatalStartupError(sodiumResult.error());
            StartupFailure failure{sodiumResult.error(),
                                   {StartupRecoveryAction::Retry, StartupRecoveryAction::Quit},
                                   {},
                                   {}};
            RecoveryModeWindow recovery(std::move(failure));
            recovery.exec();
            if (recovery.selectedAction() == StartupRecoveryAction::Retry) {
                continue;
            }
            return 1;
        }

        infrastructure::database::InternalDatabaseManager database(
            paths.value().internalDatabaseFile, paths.value().backupDirectory);
        auto databaseResult = database.open();
        if (databaseResult) {
            databaseResult = database.migrate();
        }
        if (!databaseResult) {
            logFatalStartupError(databaseResult.error());
            StartupFailure failure{databaseResult.error(),
                                   {StartupRecoveryAction::Retry, StartupRecoveryAction::Quit},
                                   paths.value().internalDatabaseFile,
                                   paths.value().backupDirectory};
            const auto closeResult = database.close();
            if (!closeResult) {
                qCritical().noquote() << closeResult.error().technicalMessage;
            }
            RecoveryModeWindow recovery(std::move(failure));
            recovery.exec();
            if (recovery.selectedAction() == StartupRecoveryAction::Retry) {
                continue;
            }
            return 1;
        }

        qInfo().noquote() << QStringLiteral("DevHub bootstrap completed in %1 ms")
                                 .arg(startupTimer.elapsed());
        const int result = runApplication(paths.value(), database, logManager, startupTimer,
                                          recoveredFromUncleanExit);
        crashMarker.markClean();
        return result;
    }
}

int ApplicationBootstrapper::runApplication(const infrastructure::filesystem::ApplicationPaths& paths,
                                            infrastructure::database::InternalDatabaseManager& database,
                                            infrastructure::logging::ApplicationLogManager& logManager,
                                            QElapsedTimer& startupTimer,
                                            bool recoveredFromUncleanExit)
{
    infrastructure::settings::SqlSettingsRepository settingsRepository(database);
    application::settings::SettingsService settingsService(settingsRepository);
    const QString configuredGitPath = settingsService.value(QStringLiteral("git/executable")).toString().trimmed();
    if (configuredGitPath.isEmpty()) QSettings().remove(QStringLiteral("git/executablePath"));
    else QSettings().setValue(QStringLiteral("git/executablePath"), configuredGitPath);
    const auto& redactor = logManager.redactor();
    presentation::theme::ThemePalette::setVariant(
        settingsService.value(QStringLiteral("appearance/theme")).toString() == QStringLiteral("dark-blue")
            ? presentation::theme::ThemeVariant::DarkBlue : presentation::theme::ThemeVariant::DarkRose);
    auto* themeManager = new presentation::theme::ThemeManager(m_application);
    themeManager->setReducedMotion(settingsService.value(QStringLiteral("appearance/reducedMotion")).toBool());

    application::events::ApplicationEventHub eventHub;
    application::lifecycle::ApplicationLifecycle lifecycle(eventHub);
    application::tasks::TaskScheduler scheduler(m_idGenerator);
    application::notifications::NotificationService notifications(m_clock, m_idGenerator, eventHub);
    infrastructure::notifications::SqlNotificationRepository notificationRepository(database, redactor);
    (void)notificationRepository.prune(settingsService.value(QStringLiteral("notifications/retentionDays")).toInt());
    application::lifecycle::ShutdownCoordinator shutdown(m_application, lifecycle, scheduler, eventHub);

    infrastructure::account::SqlAccountRepository accountRepository(database, m_idGenerator);
    infrastructure::account::SodiumPasswordHasher passwordHasher;
    infrastructure::filesystem::LocalUserDirectoryProvisioner directoryProvisioner(paths.usersDirectory);
    infrastructure::filesystem::LocalProfileResetter profileResetter(
        paths.localDataDirectory, paths.usersDirectory, database);
    application::security::PasswordTaskService passwordTasks(scheduler, passwordHasher);
    application::authentication::AuthenticationService authentication(
        accountRepository, directoryProvisioner, profileResetter, m_idGenerator, passwordTasks);
    application::authentication::SessionController session;

    std::optional<domain::account::Account> storedAccount;
    auto accountLookup = accountRepository.findSingleAccount();
    if (!accountLookup) {
        logFatalStartupError(accountLookup.error());
        StartupFailure failure{accountLookup.error(), {StartupRecoveryAction::Retry, StartupRecoveryAction::Quit}, {}, {}};
        RecoveryModeWindow recovery(std::move(failure));
        recovery.exec();
        if (recovery.selectedAction() == StartupRecoveryAction::Retry) {
            accountLookup = accountRepository.findSingleAccount();
        }
        if (!accountLookup) {
            return 1;
        }
    }
    if (accountLookup.value().has_value()) {
        storedAccount.emplace(*accountLookup.value());
        session.accountAvailable();
    }

    const bool initiallySigningIn = storedAccount.has_value();
    const QString initialUsername = initiallySigningIn
        ? storedAccount->displayUsername().value() : QString();
    m_application.setQuitOnLastWindowClosed(false);
    presentation::authentication::AuthenticationWindow authenticationWindow(
        initiallySigningIn, initialUsername, authentication, session, nullptr, &startupTimer,
        settingsService.value(QStringLiteral("security/keepSessionDefault")).toBool(), themeManager);
    const bool authenticated = authenticationWindow.exec() == QDialog::Accepted
        && session.state() == application::authentication::SessionState::SignedIn;
    if (!authenticated) {
        shutdown.requestShutdown();
        return 0;
    }

    auto inactivityTimeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::minutes(settingsService.value(QStringLiteral("security/inactivityTimeoutMinutes")).toInt())).count();
    application::authentication::SystemMonotonicClock inactivityClock;
        application::authentication::InactivityMonitor inactivityMonitor(
        m_application, session, inactivityClock, static_cast<int>(inactivityTimeout));

    QObject::connect(&scheduler,
                     &application::tasks::TaskScheduler::progressChanged,
                     &eventHub,
                     &application::events::ApplicationEventHub::taskProgressChanged);

    const bool enteredReady = lifecycle.transitionTo(application::lifecycle::ApplicationLifecycleState::Ready);
    Q_ASSERT(enteredReady);
    Q_UNUSED(enteredReady);

    infrastructure::workspace::SqlWorkspaceRepository workspaceRepository(database);
    application::workspaces::WorkspaceValidator workspaceValidator;
    application::workspaces::WorkspaceManager workspaceManager(
        workspaceRepository, workspaceValidator, m_idGenerator, m_clock, session.account()->id());
    application::workspaces::WorkspaceContext workspaceContext;
    application::workspaces::WorkspaceStateStore workspaceState;
    application::workspaces::WorkspaceRelocationService relocation(
        workspaceRepository, workspaceValidator, session.account()->id());
    infrastructure::workspace::WindowsTerminalLauncher terminalLauncher(settingsService);
    infrastructure::services::SqlServiceDefinitionRepository serviceRepository(database, session.account()->id());
    application::services::QProcessLauncher processLauncher;
    application::services::ServiceCoordinator serviceCoordinator(serviceRepository, processLauncher);
    QObject::connect(&serviceCoordinator, &application::services::ServiceCoordinator::stateChanged,
        &notifications, [&notifications](const QString& id, const application::services::ServiceRuntimeState& state) {
            if (state.lifecycle == application::services::ServiceLifecycleState::Failed) {
                application::notifications::NotificationDraft draft;
                draft.severity = application::notifications::NotificationSeverity::Error;
                draft.category = application::notifications::NotificationCategory::Service;
                draft.title = QStringLiteral("Service stopped unexpectedly");
                draft.message = QStringLiteral("A managed service failed. Open Services to review its status.");
                draft.deduplicationKey = QStringLiteral("service-failed:%1").arg(id);
                (void)notifications.post(std::move(draft));
            } else if (state.health == application::services::ServiceHealthState::Unhealthy) {
                application::notifications::NotificationDraft draft;
                draft.severity = application::notifications::NotificationSeverity::Warning;
                draft.category = application::notifications::NotificationCategory::Port;
                draft.title = QStringLiteral("Expected service port is unavailable");
                draft.message = QStringLiteral("A service did not open all of its expected ports. Open Services to inspect it.");
                draft.deduplicationKey = QStringLiteral("service-port-unavailable:%1").arg(id);
                (void)notifications.post(std::move(draft));
            }
        });
    infrastructure::ports::WindowsPortProvider portProvider;
    infrastructure::ports::WindowsProcessInspector processInspector;
    application::ports::PortMonitorService portMonitor(portProvider, processInspector);
    QTimer portRefreshTimer;
    QObject::connect(&portRefreshTimer, &QTimer::timeout, &portMonitor, [&portMonitor] { (void)portMonitor.refresh(); });
    portRefreshTimer.start(qMax(5, settingsService.value(QStringLiteral("ports/refreshIntervalSeconds")).toInt()) * 1000);
    QTimer::singleShot(0, &portMonitor, [&portMonitor] { (void)portMonitor.refresh(); });
    infrastructure::logs::SqlLogSourceRepository logSourceRepository(database, session.account()->id());
    infrastructure::database::SqlDatabaseReferenceRepository databaseReferenceRepository(database, session.account()->id());
    infrastructure::database::QtSqlConnectionFactory databaseConnectionFactory;
    const QString notesStorageRoot = QDir(paths.localDataDirectory).filePath(QStringLiteral("notes"));
    infrastructure::notes::NotesRepository notesRepository(database, session.account()->id(), notesStorageRoot);
    infrastructure::files::WindowsTrashService trashService;
    infrastructure::files::QProcessExternalEditorLauncher editorLauncher;
    application::support::SupportBundleBuilder supportBundleBuilder(paths.logDirectory, settingsService, redactor);
    application::detection::WorkspaceDetectorRegistry detectorRegistry;
    application::detection::WorkspaceScanner scanner(detectorRegistry);
    infrastructure::detection::SqlDetectionResultRepository detectionRepository(database, m_idGenerator);
    application::detection::ScanBudget scanBudget;
    const int configuredScanBudget = settingsService.value(QStringLiteral("workspaces/scanLimitMs")).toInt();
    scanBudget.wallTime = std::chrono::milliseconds(qBound(100, configuredScanBudget, 60000));
    scanBudget.validateSQLiteHeader = QSettings().value(QStringLiteral("detection/validateSQLiteHeader"), false).toBool();
    application::detection::WorkspaceScanService scanService(scheduler, scanner, detectorRegistry,
        detectionRepository, workspaceContext, m_clock, scanBudget, &settingsService);
    QObject::connect(&workspaceContext, &application::workspaces::WorkspaceContext::workspaceChanged,
        &scanService, [&workspaceContext, &scanService](const QString& workspaceId, const QString&) {
            if (workspaceId.isEmpty()) return;
            QTimer::singleShot(0, &scanService, [&workspaceContext, &scanService, workspaceId] {
                const auto& active = workspaceContext.activeWorkspace();
                if (active && active->id().toString() == workspaceId) scanService.schedule(*active);
            });
        });
    presentation::shell::MainWindow mainWindow(session, authentication, workspaceManager, workspaceContext,
                                                workspaceState, relocation, terminalLauncher, scheduler,
                                                serviceRepository, serviceCoordinator, portMonitor, logSourceRepository,
                                                databaseReferenceRepository, databaseConnectionFactory, notesRepository,
                                                notesStorageRoot, session.account()->id().toString(), trashService, editorLauncher,
                                                settingsService, *themeManager, eventHub, notificationRepository,
                                                notifications, supportBundleBuilder, redactor);
    QObject::connect(&scheduler, &application::tasks::TaskScheduler::progressChanged, &mainWindow,
        [&mainWindow, &workspaceContext](const application::tasks::TaskProgress& progress) {
            if (progress.moduleKey == QStringLiteral("workspace-detection") && progress.workspaceId
                && workspaceContext.activeWorkspace()
                && *progress.workspaceId == QUuid::fromString(workspaceContext.activeWorkspace()->id().toString()))
                mainWindow.statusBar()->showMessage(progress.message, 0);
        });
    QObject::connect(&scanService, &application::detection::WorkspaceScanService::scanFinished,
        &mainWindow, [&mainWindow](const QString&, bool success, const QString& message) {
            mainWindow.statusBar()->showMessage(message, success ? 5000 : 8000);
        });
    QObject::connect(&mainWindow, &presentation::shell::MainWindow::quitRequested,
                     &m_application, &QCoreApplication::quit);
    QObject::connect(&mainWindow, &presentation::shell::MainWindow::signOutRequested,
                     &mainWindow, [this, &mainWindow, &session, &accountRepository,
                      &authentication, &shutdown, &workspaceContext, &settingsService, themeManager] {
        workspaceContext.clear();
        session.signOut();
        mainWindow.hide();
        auto account = accountRepository.findSingleAccount();
        if (!account || !account.value().has_value()) {
            shutdown.requestShutdown();
            return;
        }
        m_application.setQuitOnLastWindowClosed(false);
        presentation::authentication::AuthenticationWindow signInWindow(
            true, account.value()->displayUsername().value(), authentication, session, nullptr, nullptr,
            settingsService.value(QStringLiteral("security/keepSessionDefault")).toBool(), themeManager);
        const bool signedIn = signInWindow.exec() == QDialog::Accepted
            && session.state() == application::authentication::SessionState::SignedIn;
        if (signedIn) {
            m_application.setQuitOnLastWindowClosed(true);
            mainWindow.restoreWorkspaceSelection();
            mainWindow.show();
        } else {
            shutdown.requestShutdown();
        }
    });
    QObject::connect(&m_application, &QCoreApplication::aboutToQuit, &session,
                     &application::authentication::SessionController::clear);
    QObject::connect(&m_application, &QCoreApplication::aboutToQuit, &mainWindow,
                     &presentation::shell::MainWindow::flushNotesForShutdown);
    m_application.setQuitOnLastWindowClosed(true);
    mainWindow.show();
    if (recoveredFromUncleanExit) mainWindow.showRecoveryNotice();
    for (const QString& argument : m_application.arguments()) {
        constexpr auto capturePrefix = "--capture-shell=";
        if (argument.startsWith(QLatin1String(capturePrefix))) {
            const QString outputPath = argument.mid(static_cast<qsizetype>(sizeof(capturePrefix) - 1));
            QTimer::singleShot(1200, &mainWindow, [this, &mainWindow, outputPath] {
                const bool saved = mainWindow.grab().save(outputPath);
                if (!saved) {
                    qCritical().noquote() << QStringLiteral("Unable to save shell capture: %1").arg(outputPath);
                }
                m_application.exit(saved ? 0 : 2);
            });
            break;
        }
    }
    return m_application.exec();
}

void ApplicationBootstrapper::logFatalStartupError(const domain::errors::AppError& error) const
{
    shared::security::RedactionService redactor;
    const auto environment = QProcessEnvironment::systemEnvironment();
    static const QRegularExpression secretPattern(QStringLiteral("(?i)(PASSWORD|PASSWD|TOKEN|SECRET|API[_-]?KEY|PRIVATE[_-]?KEY)"));
    for (const auto& key : environment.keys()) if (secretPattern.match(key).hasMatch()) {
        redactor.addSecretEnvironmentName(key); redactor.addSecretEnvironmentValue(environment.value(key));
    }
    const QString safeRecord = QStringLiteral("%1 startup failure [%2]: %3\n")
                                   .arg(m_clock.utcNow().toString(Qt::ISODateWithMs),
                                        domain::errors::errorCodeName(error.code),
                                        redactor.redact(error.technicalMessage));
    const QString temporaryDirectory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QFile logFile(QDir(temporaryDirectory).filePath(QStringLiteral("devhub-startup.log")));
    if (!temporaryDirectory.isEmpty() && logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logFile.write(safeRecord.toUtf8());
        logFile.close();
    } else {
        qCritical().noquote() << redactor.redact(safeRecord.trimmed());
    }
}

} // namespace devhub::app
