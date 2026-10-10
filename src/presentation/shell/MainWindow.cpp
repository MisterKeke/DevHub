#include "MainWindow.h"

#include "../authentication/AuthenticationWidgets.h"
#include "../authentication/ChangePasswordDialog.h"
#include "../icons/IconLoader.h"
#include "../theme/ThemePalette.h"
#include "../theme/ThemeTokens.h"
#include "../widgets/ReusableWidgets.h"
#include "../workspaces/AddWorkspaceDialog.h"
#include "../workspaces/WorkspaceListDelegate.h"
#include "../workspaces/WorkspaceListModel.h"
#include "../workspaces/WorkspaceListView.h"
#include "../git/GitPage.h"
#include "../services/ServicePage.h"
#include "../ports/PortPage.h"
#include "../logs/LogsPage.h"
#include "../databases/DatabasePage.h"
#include "../notes/NotesPage.h"
#include "../assistant/AssistantPage.h"
#include "../assistant/AssistantNavigation.h"
#include "../files/FileBrowserPage.h"
#include "../overview/OverviewPage.h"
#include "../commands/CommandPaletteDialog.h"
#include "../settings/SettingsWindow.h"
#include "../notifications/NotificationCenterDialog.h"
#include "../notifications/ToastManager.h"
#include "../settings/SupportBundleDialog.h"
#include "../../application/support/SupportBundleBuilder.h"
#include "../theme/ThemeManager.h"
#include "../../application/settings/SettingsService.h"
#include "../../application/overview/OverviewStateComposer.h"
#include "../../application/commands/CommandRegistry.h"
#include "../../application/commands/ICommandSearchProvider.h"
#include "../../application/authentication/AuthenticationService.h"
#include "../../application/authentication/SessionController.h"
#include "../../application/authentication/SessionState.h"
#include "../../application/lifecycle/ShutdownCoordinator.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QDesktopServices>
#include <QClipboard>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QMessageBox>
#include <QDir>
#include <QUrl>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QQuickView>
#include <QSignalBlocker>
#include <QQmlContext>
#include <QDialog>
#include <QVariantMap>

namespace devhub::presentation::shell {
namespace {

using devhub::presentation::icons::IconLoader;
using devhub::presentation::icons::IconName;
using devhub::presentation::theme::ColorToken;
using devhub::presentation::theme::ThemePalette;
using devhub::presentation::theme::ThemeTokens;
using devhub::presentation::theme::SizeToken;
using devhub::presentation::widgets::EmptyStateWidget;

class ModuleNavigationDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        auto adjusted = option;
        if (index.row() == 9) {
            painter->save(); painter->setPen(option.palette.mid().color());
            painter->drawLine(option.rect.topLeft(), option.rect.topRight()); painter->restore();
            adjusted.rect.adjust(0, 6, 0, 0);
        }
        QStyledItemDelegate::paint(painter, adjusted, index);
    }
};

QToolButton* makeToolButton(const QString& name, IconName iconName, QWidget* parent,
                            bool iconOnly = false)
{
    auto* button = new QToolButton(parent);
    button->setIcon(IconLoader::icon(iconName, ThemePalette::color(ColorToken::TextSecondary), 20,
                                     button->devicePixelRatioF()));
    button->setIconSize(QSize(20, 20));
    button->setText(iconOnly ? QString{} : name);
    button->setToolButtonStyle(iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    button->setToolTip(name);
    button->setAccessibleName(name);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setMinimumHeight(ThemeTokens::size(SizeToken::Control));
    if (iconOnly) button->setFixedWidth(ThemeTokens::size(SizeToken::Control));
    return button;
}

} // namespace

MainWindow::MainWindow(application::authentication::SessionController& session,
                       application::authentication::AuthenticationService& authentication,
                       application::workspaces::WorkspaceManager& workspaceManager,
                       application::workspaces::WorkspaceContext& workspaceContext,
                       application::workspaces::WorkspaceStateStore& workspaceState,
                       application::workspaces::WorkspaceRelocationService& relocation,
                       application::workspaces::ITerminalLauncher& terminalLauncher,
                       application::tasks::TaskScheduler& scheduler,
                       application::services::IServiceDefinitionRepository& serviceRepository,
                       application::services::ServiceCoordinator& serviceCoordinator,
                       application::ports::PortMonitorService& portMonitor,
                       application::logs::ILogSourceRepository& logSourceRepository,
                       application::databases::IDatabaseReferenceRepository& databaseReferences,
                       application::databases::ISqliteConnectionFactory& databaseFactory,
                       application::notes::INotesRepository& notesRepository,
                       QString notesStorageRoot,
                       QString accountId,
                       application::files::ITrashService& trashService,
                       application::files::IExternalEditorLauncher& editorLauncher,
                       application::settings::SettingsService& settingsService,
                       presentation::theme::ThemeManager& themeManager,
                       application::events::ApplicationEventHub& eventHub,
                       application::notifications::INotificationRepository& notificationRepository,
                       application::notifications::NotificationService& notificationService,
                       application::support::SupportBundleBuilder& supportBundleBuilder,
                       const shared::security::RedactionService& redactor,
                       application::assistant::AssistantController& assistantController,
                       application::assistant::ICodexExecutableProbe& assistantSetupProbe,
                       application::lifecycle::ShutdownCoordinator& shutdownCoordinator,
                       QWidget* parent)
    : QMainWindow(parent),
      m_session(session),
      m_authentication(authentication),
      m_workspaceManager(workspaceManager),
      m_workspaceContext(workspaceContext),
      m_workspaceState(workspaceState),
      m_relocation(relocation),
      m_terminalLauncher(terminalLauncher),
      m_scheduler(scheduler),
      m_serviceCoordinator(serviceCoordinator),
      m_settingsService(settingsService),
      m_assistantController(assistantController), m_assistantSetupProbe(assistantSetupProbe),
      m_shutdownCoordinator(shutdownCoordinator),
      m_themeManager(themeManager),
      m_eventHub(eventHub),
      m_notificationRepository(notificationRepository),
      m_notificationService(notificationService),
      m_supportBundleBuilder(supportBundleBuilder),
      m_redactor(redactor)
{
    setWindowTitle(QStringLiteral("DevHub"));
    setWindowIcon(QIcon(QStringLiteral(":/branding/devhub.svg")));
    setMinimumSize(ThemeTokens::size(SizeToken::MinimumWindowWidth),
                   ThemeTokens::size(SizeToken::MinimumWindowHeight));
    resize(1500, 900);

    m_rootPages = new QStackedWidget(this);
    m_rootPages->setObjectName(QStringLiteral("sessionPageHost"));
    setCentralWidget(m_rootPages);
    auto* root = new QSplitter(Qt::Horizontal, m_rootPages);
    root->setChildrenCollapsible(false);
    root->setHandleWidth(1);
    root->setObjectName(QStringLiteral("mainShell"));
    m_mainShell = root;

    auto* workspaceSidebar = new QWidget(root);
    workspaceSidebar->setObjectName(QStringLiteral("workspaceSidebar"));
    workspaceSidebar->setMinimumWidth(230);
    workspaceSidebar->setMaximumWidth(ThemeTokens::size(SizeToken::Sidebar));
    auto* workspaceLayout = new QVBoxLayout(workspaceSidebar);
    workspaceLayout->setContentsMargins(16, 18, 16, 16);
    workspaceLayout->setSpacing(12);
    auto* brandRow = new QHBoxLayout();
    auto* logo = new devhub::presentation::widgets::AppLogoWidget(workspaceSidebar);
    auto* brand = new QLabel(QStringLiteral("DevHub"), workspaceSidebar);
    brand->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::WindowTitle));
    brandRow->addWidget(logo);
    brandRow->addWidget(brand);
    brandRow->addStretch();
    workspaceLayout->addLayout(brandRow);
    auto* workspaceHeading = new QLabel(tr("WORKSPACES"), workspaceSidebar);
    workspaceHeading->setStyleSheet(QStringLiteral("color: %1; font-size: 9pt; font-weight: 600;")
                                        .arg(ThemePalette::color(ColorToken::TextMuted).name()));
    workspaceLayout->addWidget(workspaceHeading);
    m_workspaceSearch = new QLineEdit(workspaceSidebar);
    m_workspaceSearch->setObjectName(QStringLiteral("workspaceSearch"));
    m_workspaceSearch->setPlaceholderText(tr("Search workspaces"));
    m_workspaceSearch->setClearButtonEnabled(true);
    workspaceLayout->addWidget(m_workspaceSearch);
    auto* workspaceList = new devhub::presentation::workspaces::WorkspaceListView(workspaceSidebar);
    workspaceList->setObjectName(QStringLiteral("workspaceList"));
    workspaceList->setAccessibleName(tr("Workspaces"));
    workspaceList->setFrameShape(QFrame::NoFrame);
    workspaceList->setSelectionMode(QAbstractItemView::SingleSelection);
    workspaceList->setContextMenuPolicy(Qt::CustomContextMenu);
    workspaceList->setDragEnabled(false);
    workspaceList->setAcceptDrops(true);
    workspaceList->setDropIndicatorShown(true);
    m_workspaceModel = new devhub::presentation::workspaces::WorkspaceListModel(this);
    workspaceList->setModel(m_workspaceModel);
    workspaceList->setItemDelegate(new devhub::presentation::workspaces::WorkspaceListDelegate(workspaceList));
    m_workspaceList = workspaceList;
    workspaceLayout->addWidget(workspaceList, 1);
    auto* addWorkspaceButton = new QPushButton(tr("+  Add Workspace"), workspaceSidebar);
    addWorkspaceButton->setObjectName(QStringLiteral("addWorkspaceButton"));
    workspaceLayout->addWidget(addWorkspaceButton);
    auto* workspaceFooter = new QLabel(tr("Workspace files stay in place"), workspaceSidebar);
    workspaceFooter->setStyleSheet(QStringLiteral("color: %1;").arg(ThemePalette::color(ColorToken::TextMuted).name()));
    workspaceLayout->addWidget(workspaceFooter);
    auto* lockButton = new QPushButton(tr("Lock DevHub"), workspaceSidebar);
    lockButton->setObjectName(QStringLiteral("manualLockButton"));
    lockButton->setAccessibleName(tr("Lock DevHub"));
    workspaceLayout->addWidget(lockButton);

    auto* navigation = new QWidget(root);
    navigation->setObjectName(QStringLiteral("moduleNavigation"));
    navigation->setMinimumWidth(145);
    navigation->setMaximumWidth(ThemeTokens::size(SizeToken::ModuleNavigation));
    auto* navigationLayout = new QVBoxLayout(navigation);
    navigationLayout->setContentsMargins(10, 18, 10, 12);
    navigationLayout->setSpacing(4);
    auto* navHeading = new QLabel(tr("MODULES"), navigation);
    navHeading->setStyleSheet(QStringLiteral("color: %1; font-size: 9pt; font-weight: 600;")
                                  .arg(ThemePalette::color(ColorToken::TextMuted).name()));
    navigationLayout->addWidget(navHeading);
    auto* moduleList = new QListWidget(navigation);
    moduleList->setObjectName(QStringLiteral("moduleNavigationList"));
    moduleList->setAccessibleName(tr("Modules"));
    moduleList->setFrameShape(QFrame::NoFrame);
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Dashboard,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Overview")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Git,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Git")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Services,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Services")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Ports,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Ports")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Logs,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Logs")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Database,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Database")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Notes,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Notes")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Folder,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Files")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Settings,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Settings")));
    moduleList->addItem(new QListWidgetItem(IconLoader::icon(IconName::Assistant,
                                                              ThemePalette::color(ColorToken::TextSecondary),
                                                              18), tr("Assistant")));
    moduleList->setItemDelegate(new ModuleNavigationDelegate(moduleList));
    moduleList->item(9)->setSizeHint(QSize(120, 46));
    moduleList->setCurrentRow(0);
    navigationLayout->addWidget(moduleList, 1);

    m_commandRegistry = new application::commands::CommandRegistry(this);
    const QStringList navigationIds{QStringLiteral("nav.overview"), QStringLiteral("nav.git"),
        QStringLiteral("nav.services"), QStringLiteral("nav.ports"), QStringLiteral("nav.logs"),
        QStringLiteral("nav.database"), QStringLiteral("nav.notes"), QStringLiteral("nav.files"),
        QStringLiteral("nav.settings"), QStringLiteral("nav.assistant")};
    const QStringList navigationTitles{tr("Overview"), tr("Git"), tr("Services"), tr("Ports"),
        tr("Logs"), tr("Database"), tr("Notes"), tr("Files"), tr("Settings"), tr("Assistant")};
    for (int row = 0; row < navigationIds.size(); ++row) {
        if (row == presentation::assistant::AssistantNavigation::Index) {
            presentation::assistant::AssistantNavigation::registerCommand(*m_commandRegistry,
                [this](int index) { selectQuickModule(index); });
            continue;
        }
        using Category = application::commands::CommandCategory;
        const auto category = row == 1 ? Category::Git : row == 2 ? Category::Services
            : row == 3 ? Category::Ports : row == 4 ? Category::Logs
            : row == 5 ? Category::Database : row == 6 ? Category::Notes
            : row == 7 ? Category::Files : row == 8 ? Category::Settings : Category::Navigation;
        m_commandRegistry->addCommand(navigationIds[row], navigationTitles[row], category,
            [this, row] {
                if (row == 8) openQuickSettings();
                else selectQuickModule(row);
            }, {},
            tr("Open the %1 module").arg(navigationTitles[row]), {navigationTitles[row].toLower()});
    }

    auto* content = new QWidget(root);
    content->setObjectName(QStringLiteral("contentRegion"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    auto* header = new QWidget(content);
    header->setObjectName(QStringLiteral("workspaceHeader"));
    header->setStyleSheet(QStringLiteral("QWidget#workspaceHeader { background: %1; border-bottom: 1px solid %2; }")
                              .arg(ThemePalette::color(ColorToken::AppSurface).name(),
                                   ThemePalette::color(ColorToken::BorderSubtle).name()));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 14, 20, 14);
    auto* titleColumn = new QVBoxLayout();
    titleColumn->setSpacing(3);
    m_workspaceTitle = new QLabel(tr("DevHub"), header);
    m_workspaceTitle->setObjectName(QStringLiteral("pageTitle"));
    m_workspaceTitle->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::PageTitle));
    m_workspacePath = new QLabel(tr("Select a workspace to get started"), header);
    m_workspacePath->setStyleSheet(QStringLiteral("color: %1;").arg(ThemePalette::color(ColorToken::TextSecondary).name()));
    titleColumn->addWidget(m_workspaceTitle);
    titleColumn->addWidget(m_workspacePath);
    headerLayout->addLayout(titleColumn, 1);
    m_headerActions = new QWidget(header);
    auto* actions = new QHBoxLayout(m_headerActions);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);
    m_terminalButton = makeToolButton(tr("Open terminal"), IconName::Terminal, m_headerActions);
    m_restartButton = makeToolButton(tr("Restart services"), IconName::Services, m_headerActions);
    m_folderButton = makeToolButton(tr("Open folder"), IconName::Folder, m_headerActions);
    m_settingsButton = makeToolButton(tr("Settings"), IconName::Settings, m_headerActions);
    m_notificationsButton = makeToolButton(tr("Notifications"), IconName::More, m_headerActions, true);
    m_overflowButton = makeToolButton(tr("More actions"), IconName::More, m_headerActions, true);
    auto* overflowMenu = new QMenu(m_overflowButton);
    auto* terminalAction = m_commandRegistry->addCommand(QStringLiteral("workspace.terminal"), tr("Open terminal"),
        application::commands::CommandCategory::Workspace, [this] {
            if (!m_workspaceContext.activeWorkspace()) { statusBar()->showMessage(tr("Select a workspace first."), 3500); return; }
            const auto result = m_terminalLauncher.openAt(m_workspaceContext.activeWorkspace()->path().canonicalPath());
            if (!result) {
                QMessageBox message(this);
                message.setIcon(QMessageBox::Warning);
                message.setWindowTitle(tr("Could not open terminal"));
                message.setText(result.error().userMessage);
                message.setDetailedText(result.error().technicalMessage);
                message.exec();
            }
        }, [](const application::commands::CommandContext& context) {
            return application::commands::CommandAvailability{context.hasWorkspace, context.hasWorkspace ? QString{} : QObject::tr("Select a workspace first.")};
        }, tr("Open a terminal in the active workspace"), {tr("shell"), tr("console")});
    auto* folderAction = m_commandRegistry->addCommand(QStringLiteral("workspace.open-folder"), tr("Open or add workspace folder"),
        application::commands::CommandCategory::Workspace, [this] {
            const QString directory = QFileDialog::getExistingDirectory(this, tr("Open workspace folder"));
            if (directory.isEmpty()) return;
            const auto selected = m_workspaceManager.validator().validate(directory);
            if (selected) {
                auto registered = m_workspaceManager.list();
                if (!registered) { statusBar()->showMessage(registered.error().userMessage, 5000); return; }
                for (const auto& workspace : registered.value()) {
                    if (application::workspaces::WorkspaceValidator::pathsEqual(
                            workspace.path().canonicalPath(), selected.value().path.canonicalPath())) {
                        activateWorkspace(workspace.id().toString()); return;
                    }
                }
            }
            addWorkspace(directory);
        }, {}, tr("Open or register a folder as a workspace"), {tr("folder"), tr("explorer")});
    auto* restartServicesAction = new QAction(IconLoader::icon(IconName::Services,
        ThemePalette::color(ColorToken::TextSecondary), 18), tr("Restart services"), this);
    restartServicesAction->setToolTip(tr("Restart running or failed services in this workspace."));
    connect(restartServicesAction, &QAction::triggered, this, [this] {
        const auto active = m_workspaceContext.activeWorkspace();
        if (!active) {
            statusBar()->showMessage(tr("Select a workspace first."), 3500);
            return;
        }
        const QString activeWorkspaceId = active->id().toString();
        int restarted = 0;
        for (const auto& state : m_serviceCoordinator.states()) {
            if (state.definition.workspaceId != activeWorkspaceId) continue;
            if (state.lifecycle != application::services::ServiceLifecycleState::Running
                && state.lifecycle != application::services::ServiceLifecycleState::Failed) continue;
            const auto result = m_serviceCoordinator.restart(
                state.definition.id, active->path().canonicalPath());
            if (!result) statusBar()->showMessage(result.error().userMessage, 5000);
            else ++restarted;
        }
        statusBar()->showMessage(restarted == 0 ? tr("No running services to restart.")
                                                 : tr("Restarting %1 service(s)…").arg(restarted), 4000);
    });
    auto* settingsAction = m_commandRegistry->descriptor(QStringLiteral("nav.settings")).action;
    m_commandRegistry->addCommand(QStringLiteral("ports.refresh"), tr("Refresh open ports"),
        application::commands::CommandCategory::Ports, [&portMonitor] { (void)portMonitor.refresh(); }, {},
        tr("Refresh the cached open-port list"), {tr("scan"), tr("listening")});
    m_commandRegistry->addCommand(QStringLiteral("account.lock"), tr("Lock DevHub"),
        application::commands::CommandCategory::Account, [this] { emit manualLockRequested(); }, {},
        tr("Lock the current DevHub session"), {tr("security"), tr("session")});
    m_commandRegistry->addCommand(QStringLiteral("account.sign-out"), tr("Sign out"),
        application::commands::CommandCategory::Account, [this] { emit signOutRequested(); }, {},
        tr("Sign out of the current local account"), {tr("account"), tr("logout")});
    overflowMenu->addAction(terminalAction);
    overflowMenu->addAction(restartServicesAction);
    overflowMenu->addAction(folderAction);
    overflowMenu->addAction(settingsAction);
    overflowMenu->addSeparator();
    overflowMenu->addAction(m_commandRegistry->descriptor(QStringLiteral("account.lock")).action);
    overflowMenu->addAction(m_commandRegistry->descriptor(QStringLiteral("account.sign-out")).action);
    m_overflowButton->setMenu(overflowMenu);
    m_overflowButton->setPopupMode(QToolButton::InstantPopup);
    actions->addWidget(m_terminalButton);
    actions->addWidget(m_restartButton);
    actions->addWidget(m_folderButton);
    actions->addWidget(m_settingsButton);
    actions->addWidget(m_notificationsButton);
    actions->addWidget(m_overflowButton);
    headerLayout->addWidget(m_headerActions);
    auto* appMeta = new QLabel(QStringLiteral("DevHub v%1  |  Local").arg(QStringLiteral(DEVHUB_VERSION)), header);
    appMeta->setObjectName(QStringLiteral("appMeta"));
    appMeta->setStyleSheet(QStringLiteral("color: %1; font-size: 9pt;")
        .arg(ThemePalette::color(ColorToken::TextMuted).name()));
    headerLayout->addWidget(appMeta);
    contentLayout->addWidget(header);
    m_pageHost = new QStackedWidget(content);
    m_pageHost->setObjectName(QStringLiteral("mainPageHost"));
    m_overviewComposer = new application::overview::OverviewStateComposer(this);
    m_overviewPage = new devhub::presentation::overview::OverviewPage(*m_commandRegistry, m_pageHost);
    m_pageHost->addWidget(m_overviewPage);
    auto* gitPage = new devhub::presentation::git::GitPage(m_scheduler, m_pageHost, &m_settingsService);
    m_pageHost->addWidget(gitPage);
    auto* servicePage = new devhub::presentation::services::ServicePage(serviceRepository, serviceCoordinator, m_pageHost);
    m_pageHost->addWidget(servicePage);
    auto* portPage = new devhub::presentation::ports::PortPage(portMonitor, serviceCoordinator, workspaceManager, m_pageHost);
    m_pageHost->addWidget(portPage);
    auto* logsPage = new devhub::presentation::logs::LogsPage(logSourceRepository, serviceCoordinator, m_pageHost,
        m_settingsService.value(QStringLiteral("logs/maxLines")).toLongLong(),
        m_settingsService.value(QStringLiteral("logs/maxBytes")).toLongLong());
    m_pageHost->addWidget(logsPage);
    auto* databasePage = new devhub::presentation::databases::DatabasePage(databaseReferences, databaseFactory, m_pageHost,
        m_settingsService.value(QStringLiteral("database/pageSize")).toInt(),
        m_settingsService.value(QStringLiteral("database/queryHistoryEnabled")).toBool(),
        m_settingsService.value(QStringLiteral("database/queryHistoryLimit")).toInt());
    m_databasePage = databasePage;
    connect(databasePage, &devhub::presentation::databases::DatabasePage::databaseOperationFailed, this,
        [this](const QString& message) {
            application::notifications::NotificationDraft draft;
            draft.severity = application::notifications::NotificationSeverity::Error;
            draft.category = application::notifications::NotificationCategory::Database;
            draft.title = tr("Database operation failed");
            draft.message = message;
            draft.deduplicationKey = QStringLiteral("database-operation-failed");
            if (const auto workspace = m_workspaceContext.activeWorkspace())
                draft.workspaceId = QUuid::fromString(workspace->id().toString());
            (void)m_notificationService.post(std::move(draft));
        });
    m_pageHost->addWidget(databasePage);
    m_notesPage = new devhub::presentation::notes::NotesPage(notesRepository, accountId, notesStorageRoot, m_pageHost);
    m_pageHost->addWidget(m_notesPage);
    auto* filesPage = new devhub::presentation::files::FileBrowserPage(trashService, editorLauncher, m_pageHost);
    m_pageHost->addWidget(filesPage);
    auto* settingsPage = new QWidget(m_pageHost);
    auto* settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(32, 28, 32, 28);
    settingsLayout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Three));
    auto* settingsTitle = new QLabel(tr("Account security"), settingsPage);
    settingsTitle->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::PageTitle));
    auto* settingsDescription = new QLabel(
        tr("Manage the local account password used to access DevHub. This account does not encrypt workspace files."),
        settingsPage);
    settingsDescription->setWordWrap(true);
    settingsDescription->setMaximumWidth(680);
    settingsDescription->setStyleSheet(QStringLiteral("color: %1;")
                                           .arg(ThemePalette::color(ColorToken::TextSecondary).name()));
    auto* changePasswordButton = new devhub::presentation::widgets::SecondaryButton(
        tr("Change Password"), settingsPage);
    changePasswordButton->setObjectName(QStringLiteral("changePasswordButton"));
    auto* openSettingsButton = new devhub::presentation::widgets::SecondaryButton(tr("Open Settings…"), settingsPage);
    openSettingsButton->setObjectName(QStringLiteral("openSettingsButton"));
    auto* supportBundleButton = new devhub::presentation::widgets::SecondaryButton(tr("Create Support Bundle…"), settingsPage);
    supportBundleButton->setObjectName(QStringLiteral("createSupportBundleButton"));
    settingsLayout->addWidget(settingsTitle);
    settingsLayout->addWidget(settingsDescription);
    settingsLayout->addWidget(changePasswordButton, 0, Qt::AlignLeft);
    settingsLayout->addWidget(openSettingsButton, 0, Qt::AlignLeft);
    settingsLayout->addWidget(supportBundleButton, 0, Qt::AlignLeft);
    settingsLayout->addStretch();
    m_pageHost->addWidget(settingsPage);
    m_assistantPage = new devhub::presentation::assistant::AssistantPage(assistantController, m_pageHost);
    m_pageHost->addWidget(m_assistantPage);
    m_commandPalette = new devhub::presentation::commands::CommandPaletteDialog(*m_commandRegistry, this);
    m_workspaceSearchProvider = new application::commands::WorkspaceSearchProvider();
    m_servicesSearchProvider = new application::commands::ServiceSearchProvider();
    m_notesSearchProvider = new application::commands::NoteSearchProvider();
    m_databaseSearchProvider = new application::commands::DatabaseSearchProvider();
    m_settingsSearchProvider = new application::commands::SettingsSearchProvider();
    m_commandPalette->addProvider(new application::commands::CommandSearchProvider(*m_commandRegistry));
    m_commandPalette->addProvider(m_workspaceSearchProvider);
    m_commandPalette->addProvider(m_servicesSearchProvider);
    m_commandPalette->addProvider(m_notesSearchProvider);
    m_commandPalette->addProvider(m_databaseSearchProvider);
    m_commandPalette->addProvider(m_settingsSearchProvider);
    m_servicesSearchProvider->setEntries({{tr("Managed services"), tr("Review service state and controls"), QStringLiteral("nav.services"), {}}});
    m_notesSearchProvider->setEntries({{tr("Workspace notes"), tr("Open notes for the active workspace"), QStringLiteral("nav.notes"), {}}});
    m_databaseSearchProvider->setEntries({{tr("Database connections"), tr("Open saved database references"), QStringLiteral("nav.database"), {}}});
    m_settingsSearchProvider->setEntries({{tr("Account security"), tr("Change the local account password"), QStringLiteral("nav.settings"), {}}});
    auto* paletteAction = new QAction(tr("Search commands…"), this);
    paletteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    paletteAction->setShortcutContext(Qt::ApplicationShortcut);
    paletteAction->setEnabled(m_session.state() == application::authentication::SessionState::SignedIn);
    addAction(paletteAction);
    connect(paletteAction, &QAction::triggered, m_commandPalette, &devhub::presentation::commands::CommandPaletteDialog::openPalette);
    connect(&m_session, &application::authentication::SessionController::stateChanged, this,
        [paletteAction, this](application::authentication::SessionState state) {
            const bool signedIn = state == application::authentication::SessionState::SignedIn;
            paletteAction->setEnabled(signedIn);
            if (!signedIn && m_commandPalette->isVisible()) m_commandPalette->hide();
        });
    contentLayout->addWidget(m_pageHost, 1);

    root->addWidget(workspaceSidebar);
    root->addWidget(navigation);
    root->addWidget(content);
    root->setStretchFactor(0, 0);
    root->setStretchFactor(1, 0);
    root->setStretchFactor(2, 1);
    root->setSizes({ThemeTokens::size(SizeToken::Sidebar),
                    ThemeTokens::size(SizeToken::ModuleNavigation), 1065});

    m_rootPages->addWidget(m_mainShell);
    auto* lockBackground = new devhub::presentation::widgets::AuthenticationBackgroundWidget(m_rootPages);
    lockBackground->setObjectName(QStringLiteral("lockPage"));
    auto* lockLayout = new QVBoxLayout(lockBackground);
    lockLayout->setContentsMargins(24, 24, 24, 24);
    lockLayout->setAlignment(Qt::AlignCenter);
    auto* card = new devhub::presentation::widgets::CardWidget(lockBackground);
    card->setMaximumWidth(620);
    card->setStyleSheet(QStringLiteral(
        "QFrame#cardWidget { background: %1;"
        " border: 1px solid %2; border-radius: 18px; }")
        .arg(ThemePalette::color(ColorToken::PanelSurface).name(),
             ThemePalette::color(ColorToken::BorderStrong).name()));
    m_unlockWidget = new devhub::presentation::authentication::UnlockWidget(
        m_session.account().has_value() ? m_session.account()->displayUsername().value() : QString(), card);
    card->contentLayout()->addWidget(m_unlockWidget);
    lockLayout->addWidget(card);
    m_lockPage = lockBackground;
    m_rootPages->addWidget(m_lockPage);

    connect(moduleList, &QListWidget::currentRowChanged, this, &MainWindow::selectPage);
    connect(addWorkspaceButton, &QPushButton::clicked, this, [this] { addWorkspace(); });
    connect(m_workspaceSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        QString selectedId = m_workspaceList->currentIndex().data(
            devhub::presentation::workspaces::WorkspaceListModel::WorkspaceIdRole).toString();
        if (selectedId.isEmpty() && m_workspaceContext.activeWorkspace())
            selectedId = m_workspaceContext.activeWorkspace()->id().toString();
        m_workspaceModel->setFilter(text);
        const int row = m_workspaceModel->rowForId(selectedId);
        if (row >= 0) m_workspaceList->setCurrentIndex(m_workspaceModel->index(row, 0));
    });
    connect(workspaceList->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) { activateWorkspace(current.data(devhub::presentation::workspaces::WorkspaceListModel::WorkspaceIdRole).toString()); });
    connect(workspaceList, &QListView::doubleClicked, this,
            [this](const QModelIndex& index) { activateWorkspace(index.data(devhub::presentation::workspaces::WorkspaceListModel::WorkspaceIdRole).toString()); });
    connect(workspaceList, &QWidget::customContextMenuRequested, this, &MainWindow::showWorkspaceMenu);
    connect(workspaceList, &devhub::presentation::workspaces::WorkspaceListView::folderDropped,
            this, [this](const QString& path) { addWorkspace(path); });
    connect(portPage, &devhub::presentation::ports::PortPage::openWorkspaceRequested,
            this, [this](const QString& workspaceId) { activateWorkspace(workspaceId); });
    connect(&m_workspaceContext, &application::workspaces::WorkspaceContext::workspaceChanged,
            this, [this, gitPage, servicePage, logsPage, databasePage, filesPage, &portMonitor](const QString& workspaceId, const QString& path) {
        const auto& active = m_workspaceContext.activeWorkspace();
        m_restartButton->setEnabled(active.has_value());
        m_observedLogBytes = 0;
        m_recentLogLines.clear();
        m_workspaceTitle->setText(active ? active->displayName() : tr("DevHub"));
        m_workspacePath->setText(active ? path : tr("Select a workspace to get started"));
        setWindowTitle(active ? QStringLiteral("%1 — DevHub").arg(active->displayName()) : QStringLiteral("DevHub"));
        application::commands::CommandContext commandContext;
        commandContext.hasWorkspace = active.has_value();
        commandContext.workspaceId = workspaceId;
        commandContext.workspaceName = active ? active->displayName() : QString{};
        commandContext.workspacePath = path;
        m_commandRegistry->setContext(commandContext);
        m_overviewComposer->setWorkspace(workspaceId, commandContext.workspaceName, path);
        const auto section = [](QString id, QString title, QString summary, QStringList actions = {},
                                QString error = {}, QStringList details = {},
                                QString status = {}, QString tone = {}) {
            application::overview::OverviewSection value;
            value.id = std::move(id); value.title = std::move(title); value.summary = std::move(summary);
            value.actions = std::move(actions); value.error = std::move(error);
            value.details = std::move(details); value.statusLabel = std::move(status);
            value.statusTone = std::move(tone);
            return value;
        };
        if (active) {
            m_overviewComposer->updateSection(section(QStringLiteral("git"), tr("Git status"),
                tr("Refresh Git to inspect the current branch and working tree."), {QStringLiteral("nav.git")}, {},
                {tr("Branch, sync, and changed files appear after refresh.")}, tr("Needs refresh"), QStringLiteral("info")));
            const auto serviceStates = m_serviceCoordinator.states();
            int running = 0; int failed = 0;
            QStringList serviceDetails;
            for (const auto& state : serviceStates) {
                running += state.lifecycle == application::services::ServiceLifecycleState::Running ? 1 : 0;
                failed += state.lifecycle == application::services::ServiceLifecycleState::Failed ? 1 : 0;
                if (serviceDetails.size() < 4) {
                    QString lifecycle = tr("Stopped");
                    switch (state.lifecycle) {
                    case application::services::ServiceLifecycleState::Starting: lifecycle = tr("Starting"); break;
                    case application::services::ServiceLifecycleState::Running: lifecycle = tr("Running"); break;
                    case application::services::ServiceLifecycleState::Stopping: lifecycle = tr("Stopping"); break;
                    case application::services::ServiceLifecycleState::Failed: lifecycle = tr("Failed"); break;
                    case application::services::ServiceLifecycleState::Stopped: break;
                    }
                    serviceDetails.append(tr("%1  ·  %2").arg(state.definition.name, lifecycle));
                }
            }
            m_overviewComposer->updateSection(section(QStringLiteral("services"), tr("Services and run"),
                tr("%1 running · %2 failed · %3 tracked").arg(running).arg(failed).arg(serviceStates.size()),
                {QStringLiteral("nav.services"), QStringLiteral("workspace.terminal")}, {}, serviceDetails,
                failed > 0 ? tr("Attention") : running > 0 ? tr("Running") : tr("Stopped"),
                failed > 0 ? QStringLiteral("error") : running > 0 ? QStringLiteral("success") : QStringLiteral("warning")));
            const auto portRecords = portMonitor.records();
            const int openPorts = static_cast<int>(portRecords.size());
            QStringList portDetails;
            for (const auto& record : portRecords) {
                if (portDetails.size() >= 4) break;
                const QString processName = record.processName.isEmpty() ? tr("Unknown process") : record.processName;
                portDetails.append(tr("%1  ·  %2  ·  %3")
                    .arg(record.localPort).arg(application::ports::protocolName(record.protocol), processName));
            }
            m_overviewComposer->updateSection(section(QStringLiteral("ports"), tr("Open ports"),
                tr("%1 open port record(s) in the latest scan.").arg(openPorts),
                {QStringLiteral("nav.ports"), QStringLiteral("ports.refresh")}, {}, portDetails,
                openPorts > 0 ? tr("Listening") : tr("Clear"),
                openPorts > 0 ? QStringLiteral("success") : QStringLiteral("info")));
            m_overviewComposer->updateSection(section(QStringLiteral("logs"), tr("Recent logs"),
                tr("%1 bytes of service output received this session.").arg(m_observedLogBytes),
                {QStringLiteral("nav.logs")}, {}, m_recentLogLines,
                m_recentLogLines.isEmpty() ? tr("Waiting") : tr("Live"),
                m_recentLogLines.isEmpty() ? QStringLiteral("info") : QStringLiteral("success")));
        } else {
            for (const QString& id : {QStringLiteral("git"), QStringLiteral("services"), QStringLiteral("ports"),
                                      QStringLiteral("logs"), QStringLiteral("database"), QStringLiteral("notes")})
                m_overviewComposer->removeSection(id);
        }
        gitPage->setWorkspacePath(path, active ? std::optional<QUuid>{QUuid::fromString(active->id().toString())} : std::nullopt);
        servicePage->setWorkspace(workspaceId, path);
        logsPage->setWorkspace(workspaceId, path);
        databasePage->setWorkspace(workspaceId, path);
        m_notesPage->setWorkspace(workspaceId);
        if (active) {
            m_overviewComposer->updateSection(section(QStringLiteral("database"), tr("Database"),
                tr("%1 saved database reference(s).").arg(databasePage->referenceCount()),
                {QStringLiteral("nav.database")}, {}, databasePage->referenceNames().mid(0, 4),
                databasePage->referenceCount() > 0 ? tr("SQLite") : tr("No database"),
                databasePage->referenceCount() > 0 ? QStringLiteral("success") : QStringLiteral("info")));
            m_overviewComposer->updateSection(section(QStringLiteral("notes"), tr("Notes"),
                tr("%1 notes and folders available.").arg(m_notesPage->nodeCount()),
                {QStringLiteral("nav.notes")}, {}, m_notesPage->previewTitles(),
                m_notesPage->nodeCount() > 0 ? tr("Available") : tr("Empty"),
                m_notesPage->nodeCount() > 0 ? QStringLiteral("success") : QStringLiteral("info")));
        }
        filesPage->setWorkspace(workspaceId, path);
    });
    connect(m_overviewComposer, &application::overview::OverviewStateComposer::snapshotChanged,
            m_overviewPage, &devhub::presentation::overview::OverviewPage::setSnapshot);
    connect(gitPage, &devhub::presentation::git::GitPage::overviewUpdated, this,
        [this](const QString& summary, const QStringList& details,
               const QString& status, const QString& tone) {
            if (!m_workspaceContext.activeWorkspace()) return;
            application::overview::OverviewSection section;
            section.id = QStringLiteral("git");
            section.title = tr("Git status");
            section.summary = summary;
            section.details = details;
            section.statusLabel = status;
            section.statusTone = tone;
            section.actions = {QStringLiteral("nav.git")};
            m_overviewComposer->updateSection(std::move(section));
        });
    connect(gitPage, &devhub::presentation::git::GitPage::operationFailed, this,
        [this](const QString& title, const QString& message) {
            application::notifications::NotificationDraft draft;
            draft.severity = application::notifications::NotificationSeverity::Error;
            draft.category = application::notifications::NotificationCategory::Git;
            draft.title = title; draft.message = message;
            draft.deduplicationKey = QStringLiteral("git-failure:%1").arg(title);
            (void)m_notificationService.post(std::move(draft));
        });
    connect(&m_serviceCoordinator, &application::services::ServiceCoordinator::stateChanged, this,
        [this](const QString&, const application::services::ServiceRuntimeState&) {
            if (!m_workspaceContext.activeWorkspace()) return;
            const auto states = m_serviceCoordinator.states();
            int running = 0; int failed = 0;
            for (const auto& state : states) {
                running += state.lifecycle == application::services::ServiceLifecycleState::Running ? 1 : 0;
                failed += state.lifecycle == application::services::ServiceLifecycleState::Failed ? 1 : 0;
            }
            application::overview::OverviewSection section;
            section.id = QStringLiteral("services"); section.title = tr("Services and run");
            section.summary = tr("%1 running · %2 failed · %3 tracked").arg(running).arg(failed).arg(states.size());
            for (const auto& service : states) {
                if (section.details.size() >= 4) break;
                QString lifecycle = tr("Stopped");
                switch (service.lifecycle) {
                case application::services::ServiceLifecycleState::Starting: lifecycle = tr("Starting"); break;
                case application::services::ServiceLifecycleState::Running: lifecycle = tr("Running"); break;
                case application::services::ServiceLifecycleState::Stopping: lifecycle = tr("Stopping"); break;
                case application::services::ServiceLifecycleState::Failed: lifecycle = tr("Failed"); break;
                case application::services::ServiceLifecycleState::Stopped: break;
                }
                section.details.append(tr("%1  ·  %2").arg(service.definition.name, lifecycle));
            }
            section.statusLabel = failed > 0 ? tr("Attention") : running > 0 ? tr("Running") : tr("Stopped");
            section.statusTone = failed > 0 ? QStringLiteral("error")
                : running > 0 ? QStringLiteral("success") : QStringLiteral("warning");
            section.actions = {QStringLiteral("nav.services"), QStringLiteral("workspace.terminal")};
            m_overviewComposer->updateSection(std::move(section));
        });
    connect(&portMonitor, &application::ports::PortMonitorService::recordsChanged, this,
        [this](const QVector<application::ports::PortRecord>& records) {
            if (!m_workspaceContext.activeWorkspace()) return;
            application::overview::OverviewSection section;
            section.id = QStringLiteral("ports"); section.title = tr("Open ports");
            section.summary = tr("%1 open port record(s) in the latest scan.").arg(records.size());
            for (const auto& record : records) {
                if (section.details.size() >= 4) break;
                const QString processName = record.processName.isEmpty() ? tr("Unknown process") : record.processName;
                section.details.append(tr("%1  ·  %2  ·  %3")
                    .arg(record.localPort).arg(application::ports::protocolName(record.protocol), processName));
            }
            section.statusLabel = records.isEmpty() ? tr("Clear") : tr("Listening");
            section.statusTone = records.isEmpty() ? QStringLiteral("info") : QStringLiteral("success");
            section.actions = {QStringLiteral("nav.ports"), QStringLiteral("ports.refresh")};
            m_overviewComposer->updateSection(std::move(section));
        });
    connect(&portMonitor, &application::ports::PortMonitorService::refreshStarted, this,
        [this] {
            if (!m_workspaceContext.activeWorkspace()) return;
            application::overview::OverviewSection section;
            section.id = QStringLiteral("ports"); section.title = tr("Open ports");
            section.summary = tr("Refreshing open ports…"); section.loading = true;
            section.statusLabel = tr("Scanning"); section.statusTone = QStringLiteral("info");
            section.actions = {QStringLiteral("nav.ports"), QStringLiteral("ports.refresh")};
            m_overviewComposer->updateSection(std::move(section));
        });
    connect(&m_serviceCoordinator, &application::services::ServiceCoordinator::outputAvailable, this,
        [this](const QString&, const QByteArray& bytes, bool) {
            m_observedLogBytes += bytes.size();
            const auto lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (QString line : lines) {
                line = m_redactor.redact(line.trimmed());
                if (line.isEmpty()) continue;
                m_recentLogLines.append(line.left(180));
            }
            while (m_recentLogLines.size() > 4) m_recentLogLines.removeFirst();
            if (!m_workspaceContext.activeWorkspace()) return;
            application::overview::OverviewSection section;
            section.id = QStringLiteral("logs"); section.title = tr("Recent logs");
            section.summary = tr("%1 bytes of service output received this session.").arg(m_observedLogBytes);
            section.details = m_recentLogLines;
            section.statusLabel = tr("Live"); section.statusTone = QStringLiteral("success");
            section.actions = {QStringLiteral("nav.logs")};
            m_overviewComposer->updateSection(std::move(section));
        });
    connect(&portMonitor, &application::ports::PortMonitorService::refreshFailed, this,
        [this](const QString& message) {
            if (!m_workspaceContext.activeWorkspace()) return;
            application::overview::OverviewSection section;
            section.id = QStringLiteral("ports"); section.title = tr("Open ports");
            section.summary = tr("The latest scan failed; showing the previous cached results.");
            section.error = message;
            section.statusLabel = tr("Scan failed"); section.statusTone = QStringLiteral("error");
            section.actions = {QStringLiteral("nav.ports"), QStringLiteral("ports.refresh")};
            m_overviewComposer->updateSection(std::move(section));
        });
    connect(&m_serviceCoordinator, &application::services::ServiceCoordinator::stateChanged, this,
        [this](const QString&, const application::services::ServiceRuntimeState&) {
            if (m_closeAfterServicesStop && !m_serviceCoordinator.hasRunningServices())
                QTimer::singleShot(0,this,[this]{close();});
        });
    connect(lockButton, &QPushButton::clicked, this, &MainWindow::manualLockRequested);
    connect(this, &MainWindow::manualLockRequested, &m_session, &application::authentication::SessionController::beginLock);
    connect(&m_session, &application::authentication::SessionController::stateChanged,
            this, &MainWindow::updateSessionPage);
    connect(&m_session, &application::authentication::SessionController::stateChanged, this,
            [this](application::authentication::SessionState state) {
                if (state == application::authentication::SessionState::Locking && m_notesPage)
                    (void)m_notesPage->flushPendingSave();
            });
    connect(m_unlockWidget, &devhub::presentation::authentication::UnlockWidget::unlockRequested,
            this, &MainWindow::submitUnlock);
    connect(m_unlockWidget, &devhub::presentation::authentication::UnlockWidget::signOutRequested,
            this, &MainWindow::signOutRequested);
    connect(m_unlockWidget, &devhub::presentation::authentication::UnlockWidget::quitRequested,
            this, [this] { close(); });
    connect(&m_authentication, &application::authentication::AuthenticationService::busyChanged,
            m_unlockWidget, &devhub::presentation::authentication::UnlockWidget::setBusy);
    connect(&m_authentication, &application::authentication::AuthenticationService::retryDelayChanged,
            m_unlockWidget, &devhub::presentation::authentication::UnlockWidget::setRetryRemaining);
    connect(m_terminalButton, &QToolButton::clicked, terminalAction, &QAction::trigger);
    connect(m_restartButton, &QToolButton::clicked, restartServicesAction, &QAction::trigger);
    connect(m_folderButton, &QToolButton::clicked, folderAction, &QAction::trigger);
    connect(m_settingsButton, &QToolButton::clicked, settingsAction, &QAction::trigger);
    m_toastManager = new devhub::presentation::notifications::ToastManager(*this, this);
    connect(&m_eventHub, &application::events::ApplicationEventHub::notificationRaised, this,
        [this](application::notifications::Notification notification) {
            const auto persisted = m_notificationRepository.add(notification);
            if (!persisted) statusBar()->showMessage(persisted.error().userMessage, 6000);
            m_toastManager->enqueue(std::move(notification));
        });
    connect(m_notificationsButton, &QToolButton::clicked, this, [this] {
        const QString workspaceId = m_workspaceContext.activeWorkspace()
            ? m_workspaceContext.activeWorkspace()->id().toString() : QString{};
        devhub::presentation::notifications::NotificationCenterDialog dialog(m_notificationRepository, workspaceId, this);
        dialog.exec();
    });
    m_terminalButton->setEnabled(terminalAction->isEnabled());
    connect(terminalAction, &QAction::changed, m_terminalButton, [terminalAction, this] {
        m_terminalButton->setEnabled(terminalAction->isEnabled());
    });
    connect(changePasswordButton, &QPushButton::clicked, this, [this] {
        if (!m_session.account().has_value()) {
            return;
        }
        devhub::presentation::authentication::ChangePasswordDialog dialog(
            m_session.account()->normalizedUsername(), m_authentication, this);
        dialog.exec();
    });
    connect(openSettingsButton, &QPushButton::clicked, this, [this, databasePage] {
        const QString workspaceId = m_workspaceContext.activeWorkspace()
            ? m_workspaceContext.activeWorkspace()->id().toString() : QString{};
        devhub::presentation::settings::SettingsWindow dialog(m_settingsService, m_themeManager, workspaceId, this, &m_assistantController, &m_assistantSetupProbe);
        connect(&dialog, &devhub::presentation::settings::SettingsWindow::settingsApplied, &m_assistantController, [this] {
            m_assistantController.applyConfiguration({m_settingsService.value(QStringLiteral("assistant/executable")).toString(), m_settingsService.value(QStringLiteral("assistant/model")).toString()});
            if (m_session.account()) m_assistantController.setPersistenceContext(m_session.account()->id().toString(),
                m_settingsService.value(QStringLiteral("assistant/restoreConversation")).toBool());
        });
        connect(&dialog, &devhub::presentation::settings::SettingsWindow::changePasswordRequested,
                &dialog, [this] {
            if (!m_session.account()) return;
            devhub::presentation::authentication::ChangePasswordDialog passwordDialog(
                m_session.account()->normalizedUsername(), m_authentication, this);
            passwordDialog.exec();
        });
        if (dialog.exec() == QDialog::Accepted) {
            databasePage->setQueryHistoryPreferences(
                m_settingsService.value(QStringLiteral("database/queryHistoryEnabled")).toBool(),
                m_settingsService.value(QStringLiteral("database/queryHistoryLimit")).toInt());
        }
    });
    connect(supportBundleButton, &QPushButton::clicked, this, [this] {
        QStringList status;
        status << tr("Workspace selected: %1").arg(m_workspaceContext.activeWorkspace() ? tr("yes") : tr("no"));
        const auto serviceStates = m_serviceCoordinator.states();
        int running = 0; for (const auto& state : serviceStates)
            running += state.lifecycle == application::services::ServiceLifecycleState::Running ? 1 : 0;
        status << tr("Managed services running: %1 of %2").arg(running).arg(serviceStates.size());
        status << tr("Open-port records cached: %1").arg(m_workspaceContext.activeWorkspace() ? tr("available in Ports") : tr("not loaded"));
        status.append(m_assistantController.diagnosticsSnapshot());
        devhub::presentation::settings::SupportBundleDialog dialog(m_supportBundleBuilder, status, this);
        dialog.exec();
    });
    // The main workspace shell and overview are rendered by Qt Quick. Existing module
    // controllers remain alive and are hosted in a separate window until their QML
    // views are migrated, so no workflows or data access paths are dropped.
    m_moduleWindow = new QDialog(this);
    m_moduleWindow->setWindowTitle(tr("DevHub — Workspace tools"));
    m_moduleWindow->resize(1120, 760);
    m_moduleWindow->setMinimumSize(720, 520);
    auto* moduleLayout = new QVBoxLayout(m_moduleWindow);
    moduleLayout->setContentsMargins(0, 0, 0, 0);
    moduleLayout->addWidget(m_pageHost);
    connect(m_moduleWindow, &QDialog::finished, this, [this] {
        if (m_quickModule == 0) return;
        m_quickModule = 0;
        emit shellChanged();
    });
    m_modulePages = {m_overviewPage, gitPage, servicePage, portPage, logsPage,
                     databasePage, m_notesPage, filesPage, settingsPage, m_assistantPage};

    auto* quickView = new QQuickView;
    quickView->setResizeMode(QQuickView::SizeRootObjectToView);
    quickView->rootContext()->setContextProperty(QStringLiteral("shellController"), this);
    quickView->rootContext()->setContextProperty(QStringLiteral("workspaceModel"), m_workspaceModel);
    quickView->rootContext()->setContextProperty(QStringLiteral("themeController"), &m_themeManager);
    quickView->setSource(QUrl(QStringLiteral("qrc:/qt/qml/DevHub/MainShell.qml")));
    m_quickShell = QWidget::createWindowContainer(quickView, m_rootPages);
    m_quickShell->setObjectName(QStringLiteral("quickMainShell"));
    m_rootPages->insertWidget(0, m_quickShell);
    m_rootPages->setCurrentWidget(m_quickShell);
    connect(m_overviewComposer, &application::overview::OverviewStateComposer::snapshotChanged,
            this, [this] { emit shellChanged(); });
    connect(&m_workspaceContext, &application::workspaces::WorkspaceContext::workspaceChanged,
            this, [this] { emit shellChanged(); });
    connect(m_workspaceModel, &QAbstractItemModel::modelReset, this, [this] { emit shellChanged(); });
    statusBar()->hide();
    statusBar()->setSizeGripEnabled(false);
    restoreSafeGeometry();
    updateHeaderForWidth();
    updateSessionPage(m_session.state());
    reloadWorkspaces();
    const int restoredPage = m_settingsService.value(QStringLiteral("general/restoreLastPage")).toBool()
        ? QSettings().value(QStringLiteral("window/lastPage"), 0).toInt() : 0;
    if ((restoredPage > 0 && restoredPage < 8) || restoredPage == 9) selectQuickModule(restoredPage);
}

void MainWindow::restoreWorkspaceSelection()
{
    reloadWorkspaces();
}

QString MainWindow::shellWorkspaceTitle() const
{
    const auto& active = m_workspaceContext.activeWorkspace();
    return active ? active->displayName() : tr("DevHub");
}

QString MainWindow::shellWorkspacePath() const
{
    const auto& active = m_workspaceContext.activeWorkspace();
    return active ? active->path().canonicalPath() : tr("Select a workspace to get started");
}

QString MainWindow::shellWorkspaceId() const
{
    const auto& active = m_workspaceContext.activeWorkspace();
    return active ? active->id().toString() : QString{};
}

QVariantList MainWindow::overviewSections() const
{
    const auto snapshot = m_overviewComposer->snapshot();
    const QStringList order{QStringLiteral("git"), QStringLiteral("services"), QStringLiteral("ports"),
                            QStringLiteral("logs"), QStringLiteral("database"), QStringLiteral("notes")};
    const QStringList icons{QStringLiteral("⑂"), QStringLiteral("▷"), QStringLiteral("◉"),
                            QStringLiteral("≡"), QStringLiteral("▤"), QStringLiteral("▧")};
    const QStringList labels{tr("Git Status"), tr("Services / Run"), tr("Open Ports"),
                             tr("Recent Logs"), tr("Database (SQLite)"), tr("Notes")};
    const QStringList actions{tr("Open Git"), tr("Manage Services"), tr("View Ports"),
                              tr("Open Logs"), tr("Browse Database"), tr("Open Notes")};
    QVariantList result;
    for (qsizetype i = 0; i < order.size(); ++i) {
        QVariantMap item;
        const auto it = snapshot.sections.constFind(order.at(i));
        if (it != snapshot.sections.cend()) {
            item.insert(QStringLiteral("summary"), it->summary);
            item.insert(QStringLiteral("status"), it->statusLabel);
            item.insert(QStringLiteral("tone"), it->statusTone);
            item.insert(QStringLiteral("details"), it->details);
            item.insert(QStringLiteral("error"), it->error);
            item.insert(QStringLiteral("updated"), it->updatedAt.isValid()
                ? tr("Current · updated %1").arg(it->updatedAt.toLocalTime().toString(QStringLiteral("HH:mm:ss"))) : QString{});
            item.insert(QStringLiteral("loading"), it->loading);
        }
        item.insert(QStringLiteral("id"), order.at(i));
        item.insert(QStringLiteral("title"), labels.at(i));
        item.insert(QStringLiteral("icon"), icons.at(i));
        item.insert(QStringLiteral("action"), shellWorkspaceTitle() == tr("DevHub") ? QString{} : actions.at(i));
        item.insert(QStringLiteral("module"), static_cast<int>(i + 1));
        result.append(item);
    }
    return result;
}

void MainWindow::selectQuickModule(int index)
{
    if (index < 0 || index >= m_modulePages.size()) return;
    QSettings().setValue(QStringLiteral("window/lastPage"), index);
    if (index == 8) {
        openQuickSettings();
        return;
    }
    m_quickModule = index;
    emit shellChanged();
    if (index == 0) {
        m_moduleWindow->hide();
        return;
    }
    m_pageHost->setCurrentWidget(m_modulePages.at(index));
    if (index == 9) m_assistantPage->activate();
    m_moduleWindow->setWindowTitle(tr("DevHub — %1").arg(QStringList{
        tr("Overview"), tr("Git"), tr("Services"), tr("Ports"), tr("Logs"),
        tr("Database"), tr("Notes"), tr("Files"), tr("Settings"), tr("Assistant")}.at(index)));
    m_moduleWindow->show();
    m_moduleWindow->raise();
    m_moduleWindow->activateWindow();
}

void MainWindow::activateQuickWorkspace(const QString& id) { activateWorkspace(id); }
void MainWindow::addQuickWorkspace() { addWorkspace(); }
void MainWindow::lockQuickSession() { emit manualLockRequested(); }

void MainWindow::openQuickTerminal()
{
    const auto active = m_workspaceContext.activeWorkspace();
    if (!active) { statusBar()->showMessage(tr("Select a workspace first."), 3500); return; }
    const auto result = m_terminalLauncher.openAt(active->path().canonicalPath());
    if (!result) QMessageBox::warning(this, tr("Could not open terminal"), result.error().userMessage +
        QStringLiteral("\n\n") + result.error().technicalMessage);
}

void MainWindow::restartQuickServices()
{
    const auto active = m_workspaceContext.activeWorkspace();
    if (!active) { statusBar()->showMessage(tr("Select a workspace first."), 3500); return; }
    int restarted = 0;
    for (const auto& state : m_serviceCoordinator.states()) {
        if (state.definition.workspaceId != active->id().toString()) continue;
        if (state.lifecycle != application::services::ServiceLifecycleState::Running
            && state.lifecycle != application::services::ServiceLifecycleState::Failed) continue;
        const auto result = m_serviceCoordinator.restart(state.definition.id, active->path().canonicalPath());
        if (!result) statusBar()->showMessage(result.error().userMessage, 5000);
        else ++restarted;
    }
    statusBar()->showMessage(restarted == 0 ? tr("No running services to restart.")
        : tr("Restarting %1 service(s)…").arg(restarted), 4000);
}

void MainWindow::openQuickFolder()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Open workspace folder"));
    if (directory.isEmpty()) return;
    const auto selected = m_workspaceManager.validator().validate(directory);
    if (selected) {
        const auto registered = m_workspaceManager.list();
        if (registered) for (const auto& workspace : registered.value())
            if (application::workspaces::WorkspaceValidator::pathsEqual(
                    workspace.path().canonicalPath(), selected.value().path.canonicalPath())) {
                activateWorkspace(workspace.id().toString()); return;
            }
    }
    addWorkspace(directory);
}

void MainWindow::openQuickSettings()
{
    const QString workspaceId = m_workspaceContext.activeWorkspace()
        ? m_workspaceContext.activeWorkspace()->id().toString() : QString{};
    devhub::presentation::settings::SettingsWindow dialog(m_settingsService, m_themeManager, workspaceId, this, &m_assistantController, &m_assistantSetupProbe);
    connect(&dialog, &devhub::presentation::settings::SettingsWindow::settingsApplied, &m_assistantController, [this] {
        m_assistantController.applyConfiguration({m_settingsService.value(QStringLiteral("assistant/executable")).toString(), m_settingsService.value(QStringLiteral("assistant/model")).toString()});
        if (m_session.account()) m_assistantController.setPersistenceContext(m_session.account()->id().toString(),
            m_settingsService.value(QStringLiteral("assistant/restoreConversation")).toBool());
    });
    connect(&dialog, &devhub::presentation::settings::SettingsWindow::changePasswordRequested,
            &dialog, [this] {
        if (!m_session.account()) return;
        devhub::presentation::authentication::ChangePasswordDialog passwordDialog(
            m_session.account()->normalizedUsername(), m_authentication, this);
        passwordDialog.exec();
    });
    if (dialog.exec() == QDialog::Accepted && m_databasePage) {
        m_databasePage->setQueryHistoryPreferences(
            m_settingsService.value(QStringLiteral("database/queryHistoryEnabled")).toBool(),
            m_settingsService.value(QStringLiteral("database/queryHistoryLimit")).toInt());
    }
}

bool MainWindow::flushNotesForShutdown()
{
    return !m_notesPage || m_notesPage->prepareForTransition();
}

bool MainWindow::confirmDatabaseTransition()
{
    if (!flushNotesForShutdown()) {
        QMessageBox::warning(this, tr("Note changes are not saved"),
            tr("DevHub could not save your note or its recovery copy. Keep this workspace open and retry saving before switching, signing out, or closing."));
        return false;
    }
    return !m_databasePage || m_databasePage->confirmDiscardPendingChanges();
}

void MainWindow::showRecoveryNotice()
{
    statusBar()->showMessage(tr("DevHub recovered after an unexpected exit. Git and SQL operations were not replayed, and managed services were not restarted. Check Ports for processes that may still be running; open a note to check for a recovery copy."), 20000);
}

void MainWindow::reloadWorkspaces(QString preferredId)
{
    if (preferredId.isEmpty() && m_workspaceList->currentIndex().isValid())
        preferredId = m_workspaceList->currentIndex().data(devhub::presentation::workspaces::WorkspaceListModel::WorkspaceIdRole).toString();
    if (preferredId.isEmpty()) {
        const auto saved = m_workspaceState.selected(m_workspaceManager.accountId());
        if (saved) preferredId = saved->toString();
    }
    auto result = m_workspaceManager.list();
    if (!result) { statusBar()->showMessage(result.error().userMessage, 6000); return; }
    m_workspaceModel->setWorkspaces(std::move(result).value());
    QVector<application::commands::SearchCandidate> workspaceEntries;
    for (int row = 0; row < m_workspaceModel->rowCount(); ++row) {
        const auto* workspace = m_workspaceModel->workspaceAt(row);
        if (!workspace) continue;
        const QString id = workspace->id().toString();
        const QString commandId = QStringLiteral("workspace.open.%1").arg(id);
        workspaceEntries.push_back({workspace->displayName(), workspace->path().enteredPath(), commandId, tr("Workspaces")});
        if (!m_commandRegistry->contains(commandId)) {
            m_commandRegistry->addCommand(commandId, tr("Open workspace: %1").arg(workspace->displayName()),
                application::commands::CommandCategory::Workspace, [this, id] { activateWorkspace(id); }, {},
                workspace->path().enteredPath(), {workspace->displayName().toLower(), tr("workspace")});
        }
    }
    m_workspaceSearchProvider->setEntries(std::move(workspaceEntries));
    int selectedRow = m_workspaceModel->rowForId(preferredId);
    if (selectedRow < 0) {
        for (int row = 0; row < m_workspaceModel->rowCount(); ++row) {
            const auto* workspace = m_workspaceModel->workspaceAt(row);
            if (workspace && workspace->status() == domain::workspace::WorkspaceStatus::Ready) { selectedRow = row; break; }
        }
    }
    if (selectedRow >= 0) m_workspaceList->setCurrentIndex(m_workspaceModel->index(selectedRow, 0));
    else if (m_workspaceModel->rowCount() == 0) {
        m_workspaceContext.clear(); m_workspaceState.clearSelected(m_workspaceManager.accountId());
        m_workspacePath->setText(tr("No workspace selected"));
    }
}

void MainWindow::addWorkspace(QString initialPath)
{
    devhub::presentation::workspaces::AddWorkspaceDialog dialog(this);
    if (!initialPath.isEmpty()) {
        if (auto* path = dialog.findChild<QLineEdit*>(QStringLiteral("workspacePath"))) path->setText(initialPath);
    }
    if (dialog.exec() != QDialog::Accepted) return;
    auto result = m_workspaceManager.add(dialog.displayName(), dialog.folderPath());
    if (!result) { QMessageBox::warning(this, tr("Could not add workspace"), result.error().userMessage); return; }
    const QString id = result.value().id().toString();
    reloadWorkspaces(id);
    activateWorkspace(id);
}

bool MainWindow::requestAssistantWorkspaceOpen(const QString& id,
    std::function<bool()> revalidate, std::function<void()> committed)
{
    return activateWorkspace(id, std::move(revalidate), std::move(committed));
}

bool MainWindow::activateWorkspace(const QString& id, std::function<bool()> revalidate,
    std::function<void()> committed)
{
    if (id.isEmpty() || (revalidate && !revalidate())) return false;
    const auto active = m_workspaceContext.activeWorkspace();
    if (active && active->id().toString() != id && !confirmDatabaseTransition()) {
        const QSignalBlocker blocker(m_workspaceList->selectionModel());
        m_workspaceList->setCurrentIndex(m_workspaceModel->index(m_workspaceModel->rowForId(active->id().toString()), 0));
        return false;
    }
    // Transition prompts may run a nested event loop. Recheck authorization,
    // target path and scope after those prompts, immediately before activation.
    if (revalidate && !revalidate()) return false;
    auto workspaceId = domain::workspace::WorkspaceId::fromString(id);
    if (!workspaceId) return false;
    auto result = m_workspaceManager.activate(workspaceId.value());
    if (!result) {
        statusBar()->showMessage(result.error().userMessage, 6000);
        application::notifications::NotificationDraft draft;
        draft.severity = application::notifications::NotificationSeverity::Warning;
        draft.category = application::notifications::NotificationCategory::Workspace;
        draft.title = tr("Workspace unavailable"); draft.message = result.error().userMessage;
        draft.deduplicationKey = QStringLiteral("workspace-unavailable:%1").arg(id);
        (void)m_notificationService.post(std::move(draft));
        return false;
    }
    if (committed) committed();
    m_workspaceContext.activate(result.value());
    m_workspaceState.setSelected(m_workspaceManager.accountId(), result.value().id());
    const int row = m_workspaceModel->rowForId(id);
    if (row >= 0 && m_workspaceList->currentIndex().row() != row) m_workspaceList->setCurrentIndex(m_workspaceModel->index(row, 0));
    return true;
}

void MainWindow::showWorkspaceMenu(const QPoint& position)
{
    const QModelIndex index = m_workspaceList->indexAt(position);
    if (!index.isValid()) return;
    m_workspaceList->setCurrentIndex(index);
    const auto* workspace = m_workspaceModel->workspaceAt(index.row());
    if (!workspace) return;
    const auto id = workspace->id();
    const QString name = workspace->displayName();
    const QString path = workspace->path().enteredPath();
    QMenu menu(this);
    QAction* openAction = menu.addAction(tr("Open"));
    QAction* pinAction = menu.addAction(workspace->isFavorite() ? tr("Unpin") : tr("Pin"));
    QAction* renameAction = menu.addAction(tr("Rename"));
    QAction* locateAction = menu.addAction(tr("Locate Moved Workspace…"));
    QAction* explorerAction = menu.addAction(tr("Open in Explorer"));
    QAction* copyPathAction = menu.addAction(tr("Copy Absolute Path"));
    QAction* copyRelativeAction = menu.addAction(tr("Copy Relative Path"));
    menu.addSeparator();
    QAction* removeAction = menu.addAction(tr("Remove from DevHub"));
    locateAction->setEnabled(workspace->status() != domain::workspace::WorkspaceStatus::Ready);
    QAction* selected = menu.exec(m_workspaceList->viewport()->mapToGlobal(position));
    if (selected == openAction) activateWorkspace(id.toString());
    else if (selected == pinAction) { auto r = m_workspaceManager.setFavorite(id, !workspace->isFavorite()); if (!r) statusBar()->showMessage(r.error().userMessage); reloadWorkspaces(id.toString()); }
    else if (selected == renameAction) {
        bool accepted = false; const QString value = QInputDialog::getText(this, tr("Rename Workspace"), tr("Name"), QLineEdit::Normal, name, &accepted);
        if (accepted) { auto r = m_workspaceManager.rename(id, value); if (!r) QMessageBox::warning(this, tr("Rename failed"), r.error().userMessage); reloadWorkspaces(id.toString()); }
    } else if (selected == locateAction) {
        const QString target = QFileDialog::getExistingDirectory(this, tr("Locate Workspace"));
        if (!target.isEmpty()) { auto r = m_relocation.locate(id, target); if (!r) QMessageBox::warning(this, tr("Could not locate workspace"), r.error().userMessage); reloadWorkspaces(id.toString()); }
    } else if (selected == explorerAction) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) statusBar()->showMessage(tr("The folder could not be opened."));
    } else if (selected == copyPathAction) QApplication::clipboard()->setText(path);
    else if (selected == copyRelativeAction) QApplication::clipboard()->setText(QStringLiteral("."));
    else if (selected == removeAction) {
        const auto answer = QMessageBox::question(this, tr("Remove Workspace"),
            tr("Remove “%1” from DevHub? Its project files will not be deleted.").arg(name));
        if (answer == QMessageBox::Yes) {
            if (!confirmDatabaseTransition()) return;
            auto r = m_workspaceManager.removeMetadata(id);
            if (!r) QMessageBox::warning(this, tr("Remove failed"), r.error().userMessage);
            else if (m_workspaceContext.activeWorkspace() && m_workspaceContext.activeWorkspace()->id() == id) {
                m_workspaceContext.clear(); m_workspaceState.clearSelected(m_workspaceManager.accountId());
            }
            reloadWorkspaces();
        }
    }
}

void MainWindow::updateSessionPage(application::authentication::SessionState state)
{
    if (state == application::authentication::SessionState::Locked) {
        if (m_moduleWindow) m_moduleWindow->hide();
        if (m_quickModule != 0) {
            m_quickModule = 0;
            emit shellChanged();
        }
        if (QWidget* focused = QApplication::focusWidget(); focused != nullptr && focused != m_unlockWidget) {
            m_focusBeforeLock = focused;
        }
        m_mainShell->setVisible(false);
        m_rootPages->setCurrentWidget(m_lockPage);
        setWindowTitle(tr("DevHub — Locked"));
        QTimer::singleShot(0, m_unlockWidget, &devhub::presentation::authentication::UnlockWidget::focusPassword);
    } else if (state == application::authentication::SessionState::SignedIn) {
        if (m_session.account().has_value()) {
            m_unlockWidget->setUsername(m_session.account()->displayUsername().value());
        }
        m_rootPages->setCurrentWidget(m_quickShell ? m_quickShell : m_mainShell);
        m_mainShell->setVisible(true);
        setWindowTitle(QStringLiteral("DevHub"));
        if (!m_focusBeforeLock.isNull() && m_focusBeforeLock->isVisible()) {
            m_focusBeforeLock->setFocus(Qt::OtherFocusReason);
        } else {
            m_mainShell->setFocus(Qt::OtherFocusReason);
        }
        m_focusBeforeLock.clear();
        m_unlockWidget->clearPassword();
        m_unlockWidget->showMessage({});
    }
}

void MainWindow::submitUnlock()
{
    if (!m_session.account().has_value()) {
        return;
    }
    QString password = m_unlockWidget->password();
    m_unlockWidget->clearPassword();
    m_unlockWidget->showMessage({});
    m_unlockWidget->setBusy(true);
    m_session.beginUnlock();
    QPointer<MainWindow> window(this);
    m_authentication.unlock(m_session.account()->normalizedUsername(), std::move(password),
        [window](shared::common::Result<domain::account::Account> result) mutable {
            if (!window) {
                return;
            }
            window->m_unlockWidget->setBusy(false);
            if (!result) {
                window->m_session.unlockFailed();
                window->m_unlockWidget->showMessage(result.error().userMessage);
                return;
            }
            window->m_session.unlockSucceeded(std::move(result).value());
        });
}

void MainWindow::selectPage(int index)
{
    selectQuickModule(index);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateHeaderForWidth();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_shutdownRequested) { event->ignore(); return; }
    if (m_serviceCoordinator.hasRunningServices()) {
        if (!m_closeAfterServicesStop) {
            QMessageBox dialog(this); dialog.setWindowTitle(tr("Services are running"));
            dialog.setText(tr("Stop managed services before closing DevHub?"));
            auto* stopAndExit=dialog.addButton(tr("Stop Services and Exit"),QMessageBox::AcceptRole);
            auto* cancel=dialog.addButton(tr("Cancel"),QMessageBox::RejectRole);
            dialog.setDefaultButton(qobject_cast<QPushButton*>(cancel)); dialog.exec();
            if (dialog.clickedButton()!=stopAndExit) { event->ignore(); return; }
            m_closeAfterServicesStop=true; m_serviceCoordinator.stopAll(); event->ignore(); return;
        }
        event->ignore(); return;
    }
    m_closeAfterServicesStop=false;
    if (!confirmDatabaseTransition()) { event->ignore(); return; }
    if (!isMaximized() && !isMinimized()) {
        QSettings().setValue(QStringLiteral("window/normalGeometry"), normalGeometry());
    }
    QSettings().setValue(QStringLiteral("window/lastPage"), m_pageHost->currentIndex());
    // All feature transition guards accepted the close. Keep owners alive while
    // the asynchronous Assistant participant drains its child process.
    m_shutdownRequested = true;
    m_shutdownCoordinator.requestShutdown();
    event->ignore();
}

void MainWindow::restoreSafeGeometry()
{
    const QRect saved = m_settingsService.value(QStringLiteral("general/restoreWindowGeometry")).toBool()
        ? QSettings().value(QStringLiteral("window/normalGeometry")).toRect() : QRect{};
    if (saved.isValid() && saved.width() >= minimumWidth() && saved.height() >= minimumHeight()) {
        bool visibleOnScreen = false;
        for (QScreen* screen : QApplication::screens()) {
            if (screen->availableGeometry().intersects(saved)) {
                visibleOnScreen = true;
                break;
            }
        }
        if (visibleOnScreen) {
            setGeometry(saved);
            return;
        }
    }
    QScreen* screen = QApplication::primaryScreen();
    if (screen != nullptr) {
        const QRect available = screen->availableGeometry();
        const QSize target(qMin(width(), available.width()), qMin(height(), available.height()));
        setGeometry(QRect(available.center() - QPoint(target.width() / 2, target.height() / 2), target));
    }
}

void MainWindow::updateHeaderForWidth()
{
    if (m_headerActions == nullptr) {
        return;
    }
    const bool compact = width() < 1350;
    m_terminalButton->setVisible(!compact);
    m_restartButton->setVisible(!compact);
    m_folderButton->setVisible(!compact);
    m_settingsButton->setVisible(!compact);
    m_overflowButton->setVisible(compact);
    m_workspacePath->setVisible(width() >= 1280);
}

} // namespace devhub::presentation::shell
