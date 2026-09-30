# DevHub Master Architecture and Development Plan

Document status: Baseline specification  
Specification version: 1.0  
Primary platform: Windows 11 x64  
UI framework baseline: Qt 6.8 Widgets  
Language baseline: C++20  
Last reconsidered: 2026-09-23

## 1. Purpose and authority

This document is the master implementation specification for DevHub. It combines the original product concept, the current architecture decisions, the local account requirements, and the two approved visual references.

When an implementation task conflicts with this document:

1. Stop and identify the conflict.
2. Prefer the explicit decisions in the Current Decisions section over older general ideas.
3. Do not silently replace a decision with a different architecture.
4. Record an architecture decision before making an expensive change.

The supplied images are visual references. They define layout, hierarchy, density, color direction, and overall character. They are not source-code instructions, pixel-perfect contracts, or permission to reproduce macOS window chrome on Windows.

## 2. Product definition

DevHub is a local-first desktop workspace manager for developers. A workspace is a user-selected development directory with related Git state, runnable services, ports, logs, SQLite databases, notes, and files.

The application should make this relationship visible:

    Workspace
    ├── Overview
    ├── Git
    ├── Run
    ├── Ports
    ├── Logs
    ├── Database
    ├── Notes
    └── Files

DevHub is intended to coordinate existing local development tools. It should not attempt to replace a full IDE, terminal emulator, operating-system process manager, source-control hosting service, or general-purpose database client.

### 2.1 Product principles

- Workspace is the central domain object.
- Every module has one authoritative backend state.
- Overview summarizes module state and never reimplements module logic.
- User files remain local.
- The application does not require a DevHub server.
- The local account is an application access boundary, not filesystem encryption.
- Slow work never blocks the GUI thread.
- Destructive actions are explicit, previewable, and confirmed.
- Model/view classes are used for nontrivial collections.
- Windows-specific functionality is isolated behind narrow interfaces.
- Qt-native and standard-library solutions are preferred.
- Dependencies must have a clear maintenance, security, and licensing reason.
- v1 favors reliability and scope control over feature count.

### 2.2 Explicit non-goals for v1

- Online accounts, cloud synchronization, or telemetry.
- Multiple local DevHub users.
- Windows on ARM.
- Linux implementation.
- A public binary plugin SDK.
- Built-in source-code editing.
- A terminal emulator.
- Docker or Kubernetes management.
- PostgreSQL, MySQL, Redis, or remote database browsing.
- Automatic application updates.
- Automatic crash or diagnostics uploading.
- Force push, hard reset, or aggressive Git clean.
- A visual database schema designer.
- Indiscriminate encryption of all DevHub data.
- A full secrets vault.
- Project-wide source-content indexing.

## 3. Current decisions

These decisions are binding until deliberately revised.

| Area | Decision |
|---|---|
| Qt | Target Qt 6.8 APIs |
| IDE | Qt Creator 19.0.0 is currently used but is not a runtime dependency |
| Language | C++20 |
| Build | Modern target-based CMake |
| Platform | Windows 11 x64 only for v1 |
| Linux | Architectural preparation only until after Windows v1 |
| Licensing | Personal/open-source project using Qt Community-compatible dependencies |
| UI | Qt Widgets, dark theme only |
| Account | One mandatory local account in v1 |
| Authentication | Username and Argon2id-protected password |
| Session | Never survives process exit; manual lock always requires password |
| Notes | Plain Markdown outside the project by default |
| Git | Installed Git 2.40 or newer through QProcess |
| Services | Direct executable mode preferred; shell mode explicit |
| Ports | Windows IP Helper API |
| Logs | 100,000-line default rolling limit plus a byte limit |
| SQLite | Qt SQL and QSQLITE first, hidden behind replaceable abstractions |
| SQLite writes | Read/query first; visual editing v0.5 or later |
| Files | Read-only preview; deletion through Recycle Bin where possible |
| Installer | Per-user installer, no elevation by default |
| Updates | Manual |
| Diagnostics | Local only; no telemetry or automatic upload |

## 4. Technology decisions

### 4.1 C++ and Qt

Use C++20 with Qt 6.8. C++23 features may be adopted only after the compiler, CI, and support baseline changes explicitly.

Qt modules expected in v1:

- Qt6::Core
- Qt6::Gui
- Qt6::Widgets
- Qt6::Sql
- Qt6::Concurrent where it simplifies bounded background jobs
- Qt6::Network only for local connectivity checks or future needs, not for an online DevHub service
- Qt6::Test for automated testing

Avoid Qt WebEngine. It adds substantial deployment size, attack surface, and complexity and is not required for the approved design.

### 4.2 Qt Widgets rather than QML

Use Qt Widgets for all v1 screens.

Reasons:

- DevHub is a data-dense mouse-and-keyboard desktop tool.
- QTableView, QTreeView, QListView, delegates, headers, splitters, shortcuts, and accessibility are mature.
- Git lists, ports, logs, database tables, notes trees, and file trees naturally fit model/view.
- The project remains almost entirely C++.
- Widgets simplify focus, selection, context menus, and editable tables.
- There is no need to maintain parallel Widget and QML component systems.

Do not use QQuickWidget solely to obtain gradients, rounded rectangles, or animation. Those elements can be implemented using QWidget painting, style primitives, and restrained effects.

### 4.3 Build system

Use target-based CMake. Each module should be a library target where the boundary provides value. Avoid one target per tiny folder.

Recommended developer generator:

- Ninja Multi-Config for reproducible command-line and Qt Creator builds.

Required build configurations:

- Debug
- RelWithDebInfo
- Release

Required project tooling:

- CMakePresets.json
- clang-format
- clang-tidy with a reviewed rule set
- MSVC high warning level
- warnings as errors for DevHub code in CI
- CTest

### 4.4 Dependency management

Use vcpkg manifest mode for non-Qt compiled dependencies. Pin a baseline. Do not let developer machines silently choose unrelated dependency versions.

Initial external dependency policy:

| Dependency | Status | Purpose |
|---|---|---|
| Qt 6.8 | Required | Core application framework |
| libsodium | Required | Argon2id password hashing and future secret encryption primitives |
| SQLite through QSQLITE | Required through Qt | Internal persistence and initial database browser |
| Inno Setup | Build-time | Initial Windows installer |
| Lucide or equivalent SVG icon set | Optional | Consistent open-source icons after license review |

Do not add an ORM, logging framework, dependency-injection framework, event-bus library, or Markdown web renderer until a concrete limitation justifies it.

### 4.5 Git implementation

Use the installed Git executable through QProcess.

Benefits:

- It follows the user's Git configuration.
- It works with Git Credential Manager, SSH, hooks, filters, LFS, and signing.
- It matches terminal behavior.
- Git's porcelain and explicit formatting modes provide machine-readable output.

Rules:

- Require Git 2.40 or newer.
- Pass program and arguments separately.
- Never build a shell command from branch names or paths.
- Use stable porcelain or explicit format strings.
- Disable color and paging.
- Put a double dash before user-controlled path arguments.
- Disable interactive credential prompts for background operations.
- Cap normal captured output at 16 MB.
- Cap diff previews at 8 MB.
- Treat truncation as visible state, not silent data loss.

### 4.6 SQLite implementation

Start with Qt SQL and QSQLITE as required by the current decisions.

Create replaceable interfaces so a future direct SQLite backend can be introduced without rewriting UI or use cases:

    ISqliteConnectionFactory
    IDatabaseSession
    IDatabaseSchemaReader
    IQueryExecutor
    ITablePageSource

Use QSQLITE for:

- DevHub's internal application database.
- Initial read-only workspace database browsing.
- SQL query execution.
- Later row editing if its behavior remains sufficient.

A direct SQLite C API backend becomes justified only if real requirements cannot be met, especially:

- Reliable query cancellation.
- Progress handlers.
- Authorizer callbacks.
- Online backup API access.
- Detailed statement metadata.
- Exact extended error codes.
- More predictable BLOB streaming.

Document the limitation before changing backends.

### 4.7 Cryptography

Use libsodium. Do not implement password hashing or authenticated encryption manually.

For the v1 account:

- Use Argon2id.
- Store an encoded password verifier that contains the algorithm, salt, and cost parameters.
- Store a scheme version so future migrations are possible.
- Rehash after a successful sign-in when parameters are outdated.
- Never log the password or encoded hash.

The v1 password does not encrypt repositories, external databases, source files, logs, or notes. Future DevHub-owned secret encryption must use a separate encrypted-vault design and must not reuse the password verifier as an encryption key.

## 5. Visual design specification

### 5.1 Design intent from the approved screenshots

The interface should feel:

- Professional.
- Dark and focused.
- Dense without feeling cramped.
- Developer-oriented.
- Calm, with magenta used for focus and primary action rather than everywhere.
- Structured through borders and surface contrast rather than large shadows.

The approved main-screen hierarchy is:

    Window
    ├── App/title strip
    ├── Workspace sidebar
    ├── Workspace module navigation
    └── Main content
        ├── Workspace header and actions
        └── Page content or dashboard cards

The approved authentication hierarchy is:

    Authentication window
    ├── Brand/header
    ├── Centered authentication card
    ├── Account form
    ├── Primary and secondary actions
    └── Local-only security explanation

### 5.2 Windows interpretation

The screenshots contain macOS-style traffic-light controls. DevHub targets Windows 11, so v1 should not copy them literally.

Preferred v1 approach:

- Retain the standard Windows frame and system buttons.
- Style the client area, toolbar, sidebars, cards, and content.
- Preserve Windows snapping, system menu, accessibility, DPI handling, and expected caption behavior.

A custom title bar may be considered after the core UI is stable. It requires dedicated work for hit testing, maximize behavior, Windows Snap Layouts, system menus, DPI changes, and accessibility.

### 5.3 Semantic theme tokens

Colors are examples to be tuned against real displays and contrast tests.

| Token | Initial direction |
|---|---|
| WindowBackground | #09080B |
| AppSurface | #0F0D12 |
| SidebarSurface | #111014 |
| PanelSurface | #17131A |
| RaisedSurface | #1D1720 |
| HoverSurface | #251A25 |
| ActiveSurface | #351527 |
| BorderSubtle | #2C2430 |
| BorderStrong | #513047 |
| TextPrimary | #F5F2F5 |
| TextSecondary | #A9A1AB |
| TextMuted | #756E78 |
| Accent | #FF2D7A |
| AccentHover | #FF478A |
| AccentPressed | #DB1F64 |
| AccentSoft | rgba equivalent of Accent at low opacity |
| Success | #22C978 |
| Warning | #F4B740 |
| Error | #FF525D |
| Info | #50A7FF |
| FocusRing | high-contrast Accent variant |

Do not place raw color literals in module widgets. Use ThemeManager and semantic token accessors.

### 5.4 Typography

Preferred initial typography:

- Segoe UI Variable or the Windows system UI font for interface text.
- Cascadia Mono when installed, falling back to Consolas, for logs, diffs, SQL, and paths.
- Avoid bundling fonts until licensing and deployment benefits are clear.

Suggested scale:

| Role | Size direction |
|---|---|
| Window/workspace title | 20-24 px equivalent |
| Page title | 18-22 px |
| Card title | 14-16 px semibold |
| Body | 13-14 px |
| Metadata | 11-12 px |
| Monospace content | 12-13 px |

Use device-independent layout measurements and verify at 100%, 125%, 150%, and 200% scaling.

### 5.5 Spacing and shape

Initial token direction:

- 4 px base spacing unit.
- 8 px compact internal gap.
- 12 px normal control gap.
- 16 px card padding.
- 20-24 px page padding.
- 6 px control radius.
- 8 px card radius.
- 1 px subtle borders.
- 32-36 px standard control height.
- 40-44 px prominent primary action height.

Avoid large drop shadows on every card. Use surface contrast, a subtle border, and limited accent glow for authentication focus or critical calls to action.

### 5.6 Layout metrics

Initial main-window targets:

- Recommended default size: approximately 1500 × 900 logical pixels.
- Practical minimum size: 1180 × 720 logical pixels.
- Workspace sidebar: 250-290 logical pixels.
- Module navigation: 150-180 logical pixels.
- Workspace header: approximately 76-90 logical pixels.
- Content margins: 16-24 logical pixels.

At smaller widths:

- Hide nonessential workspace path text before compressing action labels.
- Move header actions into an overflow menu.
- Allow Overview cards to reflow from three columns to two and then one.
- Keep navigation usable; do not shrink text into unreadability.

### 5.7 Reusable visual components

Create these once:

- AppLogoWidget.
- BrandHeaderWidget.
- AccentButton.
- SecondaryButton.
- DestructiveButton.
- IconButton.
- SearchField.
- PasswordField with visibility toggle.
- FormField with label, required marker, help, and error text.
- SidebarItemDelegate.
- ModuleNavigationItemDelegate.
- CardWidget.
- StatusBadge.
- EmptyStateWidget.
- InlineErrorWidget.
- LoadingOverlay.
- ToastWidget.
- ConfirmationDialog.
- TechnicalDetailsPanel.
- SplitHeaderWidget.
- MetricPill.

### 5.8 Motion

Animations are optional polish, not a prerequisite.

Allowed:

- 100-180 ms hover/focus transitions.
- Small page fade during authentication state changes.
- Progress indicators.
- Toast entrance/exit.

Avoid:

- Continuous ambient animation.
- Heavy blur.
- large parallax backgrounds.
- animation that delays access to data.
- layout animation across large tables.

Respect a reduced-motion preference.

## 6. Local account and session design

### 6.1 Account scope

v1 supports exactly one local DevHub account.

The architecture should use a UserId and account repository so multiple profiles are not impossible later. The UI, database constraints, directory layout, and use cases must nevertheless enforce one account in v1.

Minimum account data:

    user_id
    username
    username_normalized
    password_hash
    password_scheme_version
    created_at
    updated_at
    last_login_at

The encoded Argon2id password hash contains the salt and cost parameters. Separate columns may be used only if the selected library requires them.

### 6.2 Authentication screens

#### First-launch Create Account

Show when the account repository contains no account:

    Create DevHub Account

    Username
    [                         ]

    Password
    [                         ]

    Confirm Password
    [                         ]

    [ Create Account ]

Required behavior:

- All fields required.
- Trim leading/trailing username whitespace.
- Preserve the display spelling.
- Store a normalized username for comparison.
- Support Unicode but reject control characters and path separators.
- Set a documented maximum username length.
- Permit password-manager paste.
- Show password visibility toggles.
- Validate password confirmation locally.
- Hash off the GUI thread.
- Create the account and initial user directories atomically.
- If creation fails, do not leave a usable account record without its required directories.
- On success, create an authenticated session and open the main window.

#### Normal startup Sign In

Show on every later process start:

    Welcome back

    Username
    [ Konstantin             ]

    Password
    [ **************         ]

    [x] Keep me signed in for this session

    [ Sign In ]

Behavior:

- Username and password are required.
- Prefill the single account username, but keep the field editable.
- Compare the normalized submitted username with the stored account.
- Use one generic message for an invalid username or password.
- Never reveal whether a different local username exists.
- Update last_login_at only after successful authentication.
- Clear the password input after failure.
- Place focus on the password field when the prefilled username is valid.
- Enter submits the form.
- Escape does not bypass authentication; it may offer Quit.
- Closing the application always destroys the session.

#### Manual or automatic Unlock

After an authenticated application is locked, replace or cover the main UI with a smaller unlock view:

    DevHub is locked
    Signed in as Konstantin

    Password
    [ ************** ]

    [ Unlock ]
    [ Sign Out ]
    [ Quit ]

Behavior:

- Do not ask for username because the locked session identity is already known.
- Manual lock always requires the password again.
- Unlock restores the existing UI context after successful verification.
- Sign Out destroys the session and returns to the full username/password Sign In screen.
- Running managed services remain running.
- Background service output may continue to be collected subject to memory limits.
- Sensitive UI content must not remain visible beneath a transparent lock layer.

### 6.3 Keep me signed in for this session

Exact meaning:

- The authenticated state exists only in memory.
- It ends at process exit.
- It does not store the plaintext password.
- It does not create an auto-login token.
- Manual Lock always overrides it.
- When checked, automatic inactivity lock is suppressed for the current process session.
- When unchecked, the configured inactivity-lock policy applies.

The label must not imply persistence across application restarts.

### 6.4 Authentication state machine

Use an explicit state machine:

    NoAccount
        → CreatingAccount
        → SignedIn

    SignedOut
        → Authenticating
        → SignedIn

    SignedIn
        → Locking
        → Locked

    Locked
        → Unlocking
        → SignedIn

    SignedIn or Locked
        → SigningOut
        → SignedOut

    Any startup state
        → FatalAuthenticationError

Create:

    Account
    AccountId
    IAccountRepository
    SqlAccountRepository
    PasswordHasher
    AuthenticationService
    SessionController
    SessionState
    InactivityMonitor
    CreateAccountWidget
    SignInWidget
    UnlockWidget
    AuthenticationWindow

### 6.5 Password hashing

Use libsodium Argon2id.

On account creation:

1. Convert the password into a short-lived byte buffer.
2. Hash it with the configured Argon2id interactive profile.
3. Store the encoded verifier.
4. Clear transient buffers where practical.
5. Never retain the password in the Account object.

On sign-in:

1. Load the encoded verifier.
2. Verify off the GUI thread.
3. Return only success, failure, or internal error to the UI.
4. If successful and parameters are outdated, compute a replacement hash.
5. Persist the replacement in a transaction.

Qt text controls and implicitly shared strings may create memory copies. The application should minimize plaintext lifetime and overwrite controllable buffers, but it must not promise perfect password erasure from all process memory.

### 6.6 Failed attempts

Use local throttling to discourage casual repeated attempts:

- Short delay after several consecutive failures.
- Increasing delay with a sensible cap.
- Disable the submit button while verification is active.
- Do not freeze the GUI thread.
- Do not permanently lock the account.
- Do not implement security questions.

This does not prevent an attacker who can copy the hash from performing offline guessing. Password guidance and Windows account security remain important.

### 6.7 Password change

The Change Password flow requires:

- Current password.
- New password.
- Confirmation.
- Successful verification before modification.
- New Argon2id hash using current parameters.
- Transactional update of password hash and updated_at.
- Invalidation of any future quick-unlock material.
- Continued current session after success.
- No managed-service restart.

### 6.8 Forgotten password and profile reset

There is no email recovery.

The Forgot Password action should:

1. Explain that the account is local-only.
2. Explain which DevHub-owned data will become inaccessible or reset.
3. Explain that external workspace files will not be deleted.
4. Require a typed confirmation.
5. Rename the old profile directory to a timestamped recovery location.
6. Create a fresh database and account flow.
7. Never delete the old profile automatically.

Because v1 notes are plaintext Markdown, a recovery tool may later offer to import them. It must not bypass the password to recover any future encrypted secrets.

### 6.9 Security claims

The login protects access through DevHub. It does not encrypt:

- Source repositories.
- External Git state.
- Workspace files.
- External SQLite databases.
- Log files.
- Plaintext notes.
- Processes or open ports.

This limitation must be stated during account creation and in Security settings.

## 7. High-level architecture

### 7.1 Layers

    Presentation
        Widgets, models, delegates, formatting, navigation

    Application
        Use cases, coordinators, commands, state stores, policies

    Domain
        Entities, value objects, validation, interfaces

    Infrastructure
        QSQLITE repositories, Git CLI, filesystem, settings, logging

    Platform
        Windows ports, processes, terminal launch, Recycle Bin

Dependency direction:

    Presentation → Application → Domain
    Infrastructure → Domain interfaces
    Platform → Domain interfaces

Only the composition root constructs concrete dependencies.

### 7.2 Module layout

Feature modules:

- account
- workspace
- detection
- git
- services
- ports
- logs
- database
- notes
- files
- overview
- search
- settings
- notifications

Shared infrastructure:

- errors
- tasks
- persistence
- logging
- theme
- commands
- events
- platform abstractions

### 7.3 Communication

Use:

- Commands for user intent.
- State stores for authoritative observable state.
- Typed events for facts that cross module boundaries.

Examples:

    StartServiceCommand
        → ServiceCoordinator
        → ServiceStateStore
        → ServiceStarted event
        → debounced Port refresh
        → Overview recomposition

    CommitCommand
        → GitService
        → Git state refresh
        → GitStateStore
        → Overview recomposition

Do not connect GitWidget directly to OverviewWidget. Do not pass MainWindow into modules. Do not create a string-based global event bus.

### 7.4 Composition root

ApplicationBootstrapper should:

1. Initialize Qt application metadata.
2. Resolve paths.
3. Start local logging.
4. Initialize libsodium.
5. Open the internal database.
6. Apply schema migrations.
7. Construct repositories.
8. Construct Windows providers.
9. Construct application services.
10. Determine authentication state.
11. Show Create Account or Sign In.
12. Construct the main workspace shell only after authentication.

## 8. Persistence design

### 8.1 Filesystem layout

Use QStandardPaths rather than hardcoded environment variables.

Suggested Local AppData structure:

    DevHub/
    ├── data/
    │   └── devhub.db
    ├── users/
    │   └── <user-id>/
    │       ├── notes/
    │       ├── recovery/
    │       └── cache/
    ├── logs/
    ├── cache/
    ├── backups/
    ├── crashdumps/
    └── security/

Small roaming preferences may use AppData only if roaming behavior is intentionally supported. Workspace paths and machine state belong in Local AppData.

### 8.2 Internal database

Initial tables:

    schema_migrations
    accounts
    application_settings
    workspaces
    workspace_settings
    workspace_detections
    service_definitions
    service_environment_references
    log_sources
    database_references
    note_nodes
    note_tags
    note_tag_links
    recent_files
    query_history
    notifications
    ui_state

Rules:

- Enable foreign keys on every connection.
- Use named QSqlDatabase connections.
- Create and destroy each connection in its owning thread.
- Use prepared queries and bound values.
- Use transactions for aggregate updates.
- Enable WAL for the internal database after compatibility testing.
- Use a bounded busy timeout where QSQLITE supports it.
- Never store QSqlQuery beyond the connection lifetime.
- Never expose raw SQL from Widgets.
- Store UTC timestamps consistently.
- Treat runtime PIDs as observed state, not durable truth.
- Do not store Git credentials.
- Do not store plaintext passwords.
- Persistent query history is off by default.

### 8.3 Migrations

Migration requirements:

- Monotonically increasing schema version.
- Immutable released migration scripts.
- Transactional application where SQLite permits.
- Pre-migration backup for risky changes.
- Refuse databases newer than the application.
- Never silently recreate an incompatible database.
- Test upgrades from every schema version that has actually shipped.
- Provide a recovery screen when migration fails.

### 8.4 Repositories

Define focused repositories:

    IAccountRepository
    IWorkspaceRepository
    IServiceDefinitionRepository
    ILogSourceRepository
    IDatabaseReferenceRepository
    INotesMetadataRepository
    ISettingsRepository
    INotificationRepository

Avoid a generic repository template. Each aggregate has different queries, constraints, and transactional behavior.

## 9. Concurrency and responsiveness

### 9.1 Rules

- QWidget and QAbstractItemModel mutation occurs only on the GUI thread.
- Short CPU or blocking tasks use a bounded QThreadPool.
- Long-lived event-driven workers use dedicated QThreads.
- Database connections never move between threads after opening.
- QProcess operations are asynchronous.
- No waitForFinished call on the GUI thread.
- Worker results are immutable values.
- Results include workspace ID and task generation.
- Superseded results are discarded.
- Cancellation is explicit and cooperative.
- Every high-frequency producer has backpressure.

### 9.2 Task scheduler

Create TaskScheduler with:

- Global maximum worker count.
- Per-module concurrency limits.
- Priorities.
- Cancellation tokens.
- Correlation IDs.
- Workspace ownership.
- Progress reporting.
- Stale-generation rejection.

Suggested initial policies:

- One active automatic workspace scan per workspace.
- One mutating Git operation per repository.
- Limited concurrent Git reads.
- One query worker per open database session.
- One port refresh at a time.
- Batched log delivery at a fixed maximum UI frequency.

## 10. Error model

Use a structured AppError:

    code
    category
    severity
    user_message
    technical_message
    native_code
    operation
    workspace_id
    retryable
    cancelled
    correlation_id

Categories:

- Authentication
- Security
- Persistence
- Migration
- Workspace
- Git
- Process
- Port
- Log
- Database
- FileSystem
- Permission
- Configuration
- Internal

Rules:

- Cancellation is not shown as failure.
- User text explains the action and remedy.
- Technical details are expandable.
- Secrets are redacted before logging or display.
- Repeated identical errors are deduplicated.
- Fatal startup errors use a recovery screen.
- Widgets do not invent their own exception-handling policy.

## 11. Module specifications

### 11.1 Workspace Manager

Purpose:

Maintain the durable registry and current health of development workspaces.

Responsibilities:

- Add a workspace.
- Validate its directory.
- Detect duplicates.
- Rename its display name.
- Pin and unpin.
- Search.
- Open and activate.
- Remove DevHub metadata.
- Relocate a missing workspace.
- Open Explorer.
- Open a configured terminal.
- Start an asynchronous scan.

Non-responsibilities:

- Deleting the source directory.
- Running Git logic.
- Reading every file eagerly.
- Implementing module-specific UI inside the manager.

Core types:

    Workspace
    WorkspaceId
    WorkspacePath
    WorkspaceStatus
    WorkspaceManager
    WorkspaceValidator
    WorkspaceRelocationService
    WorkspaceStateStore
    WorkspaceListModel

Edge cases:

- Missing or disconnected drive.
- UNC path unavailable.
- Directory permission loss.
- Duplicate path with different case.
- Junction or symlink cycle.
- Workspace removed during scan.
- Path moved while DevHub is closed.
- Very long Windows path.

### 11.2 Project detection

Purpose:

Infer useful workspace capabilities without modifying the workspace.

Architecture:

    IWorkspaceDetector
        detectorId()
        supportedEvidence()
        detect(ScanContext)

    WorkspaceDetectorRegistry
    WorkspaceScanner
    DetectionResult
    DetectionEvidence

Initial detectors:

- Git.
- CMake.
- Node package.
- Go module.
- Rust Cargo.
- Visual Studio solution/project.
- Dockerfile.
- Docker Compose.
- Makefile.
- Python project markers.
- README.
- SQLite candidates.
- Log candidates.

Scanning defaults:

- Maximum depth 12.
- Maximum inspected files 100,000.
- Cancellation.
- Progress.
- Time budget.
- Ignore generated/heavy directories.
- Allow a detector to inspect a normally ignored marker when explicitly required.
- Never follow an unbounded directory cycle.

### 11.3 Git

Purpose:

Provide an integrated client whose behavior remains close to the user's installed Git.

Core classes:

    GitExecutableLocator
    GitVersion
    GitCommand
    GitCommandResult
    GitCommandRunner
    GitRepositoryLocator
    GitStatusParser
    GitHistoryParser
    GitDiffParser
    GitService
    GitOperationQueue
    GitStateStore
    GitStatusModel
    GitHistoryModel
    GitBranchModel

Read features:

- Repository detection.
- Current branch.
- Upstream.
- Ahead/behind.
- Staged, unstaged, untracked, conflicted.
- Branches.
- Remotes.
- History pagination.
- Commit details.
- Diff preview.
- Ongoing merge/rebase/cherry-pick state.

Mutation rollout:

1. Stage and unstage.
2. Commit.
3. Fetch.
4. Pull with displayed repository strategy.
5. Push.
6. Checkout.
7. Branch create/rename/delete.
8. Stash create/list/apply/pop.
9. Merge with conflict reporting.

Excluded early operations:

- Force push.
- Hard reset.
- Aggressive clean.
- Automatic conflict resolution.

Threading:

- QProcess is asynchronous.
- Mutations are serialized per repository.
- Safe reads may run concurrently within limits.
- Refresh requests are coalesced.

Testing:

- Temporary repositories.
- Isolated Git configuration.
- Unicode and whitespace paths.
- Rename and conflict fixtures.
- Detached HEAD.
- Unborn branch.
- Worktree.
- Slow command and cancellation.
- Hook failure.
- Truncated output.

### 11.4 Run and Services

Purpose:

Run and supervise developer processes defined for a workspace.

Service definition:

    id
    workspace_id
    name
    program
    arguments
    shell_mode
    working_directory
    environment_references
    expected_ports
    auto_start
    restart_policy
    stop_policy
    enabled
    sort_order

Lifecycle:

    Stopped
    Starting
    Running
    Degraded
    Stopping
    Failed

Core classes:

    ServiceDefinition
    ManagedService
    ServiceCoordinator
    ServiceStateStore
    ServiceListModel
    RestartPolicyEvaluator
    ServiceOutputRouter
    IProcessLauncher
    IManagedProcess
    QProcessLauncher
    WindowsManagedProcessLauncher

MVP uses QProcess. Production-hardening should investigate CreateProcessW plus Windows Job Objects for reliable process-tree ownership.

Lock behavior:

- Manual or inactivity lock leaves services running.
- Service output continues within configured buffers.
- Unlock restores live state.

Exit behavior:

    Some services are still running.

    [ Stop Services and Exit ]
    [ Leave Services Running and Exit ]
    [ Cancel ]

Leaving services running must be honest about loss of DevHub supervision and log capture.

### 11.5 Port Monitor

Purpose:

Display local TCP and UDP endpoints with process information.

Windows APIs:

- GetExtendedTcpTable.
- GetExtendedUdpTable.
- OpenProcess with minimal query rights.
- QueryFullProcessImageName where allowed.
- GetProcessTimes for PID reuse protection.

Core classes:

    PortRecord
    IPortProvider
    WindowsPortProvider
    IProcessInspector
    WindowsProcessInspector
    PortMonitorService
    PortAssociationService
    PortStateStore
    PortTableModel

Association policy:

1. A PID owned by a DevHub-managed service is strong evidence.
2. A reliably discovered executable or working directory inside the workspace may be strong evidence.
3. An expected port alone is supporting evidence, not ownership.
4. Similar names are not sufficient.
5. Uncertain ports remain unassociated.

Termination:

- Requires confirmation.
- Revalidates PID and process creation time.
- Reports access denied.
- Does not globally elevate DevHub.
- Avoids protected/system processes.

### 11.6 Log Viewer

Purpose:

Follow files and managed-service output while keeping memory bounded.

Core classes:

    LogSource
    LogSourceId
    FileIdentity
    LogReader
    LogDecoder
    LogParser
    LogRingBuffer
    LogFilterEngine
    LogStateStore
    LogLineModel

Defaults:

- Maximum 100,000 retained lines per active source.
- Additional configurable byte limit.
- UTF-8, UTF-8 with BOM, and ASCII supported.
- Chunked reading.
- Batched model updates.

Required behavior:

- Track byte offset with qint64.
- Preserve an incomplete trailing line.
- Preserve incomplete UTF-8 sequences between chunks.
- Treat QFileSystemWatcher as a hint.
- Detect truncation.
- Detect replacement/rotation.
- Re-register watches after replacement.
- Use polling fallback.
- Pause display without losing source position.
- Apply backpressure during high output.
- Report dropped/compacted lines.
- Run regex filtering off the GUI thread.

### 11.7 SQLite Manager

Purpose:

Discover, inspect, query, and later carefully edit SQLite databases.

Core abstractions:

    DatabaseReference
    ISqliteConnectionFactory
    IDatabaseSession
    IDatabaseSchemaReader
    IQueryExecutor
    ITablePageSource
    QtSqlDatabaseSession
    QtSqlSchemaReader
    QtSqlQueryExecutor
    DatabaseStateStore
    DatabaseObjectModel
    DatabaseTableModel

Initial scope:

- Detect candidates.
- Open read-only.
- List tables, views, indexes, and triggers.
- Display schema SQL.
- Display columns and keys.
- Paginate rows in 100-500 row pages.
- Sort and filter through database queries.
- Execute user-entered SQL.
- Warn before non-read-only or schema-changing SQL.
- Session query history by default.
- Persistent history only by explicit opt-in.
- Default persistent limit 500.

Do not:

- Load a full huge table.
- Assume a row is editable without stable identity.
- Concatenate values into generated SQL.
- Claim an automatic backup guarantees recovery.

### 11.8 Notes

Purpose:

Store developer notes as portable Markdown outside the source repository.

Default location:

    %LOCALAPPDATA%\DevHub\users\<user-id>\notes\<workspace-id>\

Core classes:

    NoteId
    NoteMetadata
    NotesRepository
    INotesMetadataRepository
    NotesTreeModel
    NotesAutosaveController
    NotesRecoveryJournal

Features:

- Multiple notes.
- Folders.
- Rename.
- Pin.
- Tags.
- Markdown editor.
- Preview.
- Autosave.
- Search.
- Checkboxes.
- Code blocks.
- Export.
- Recovery after crash.

Use QSaveFile for atomic writes. Store metadata and indexes in the internal database. Do not encrypt note bodies by default. In-project notes are post-v1.

### 11.9 File Browser

Purpose:

Navigate workspace files and invoke safe external operations.

Scope:

- QFileSystemModel-backed tree.
- Expand/collapse.
- Read-only text preview with size limit.
- Open externally.
- Open in configured editor/IDE.
- Open containing folder.
- Copy absolute and relative path.
- Create file/directory.
- Rename.
- Move to Recycle Bin.

Non-scope:

- Source editing.
- Refactoring.
- Language services.
- Build errors.
- Symbol search.

Path operations must revalidate the target immediately before mutation and must never allow workspace-root deletion through an ordinary file action.

### 11.10 Overview

OverviewStateComposer consumes:

- WorkspaceStateStore.
- GitStateStore.
- ServiceStateStore.
- PortStateStore.
- LogStateStore.
- DatabaseStateStore.
- NotesStateStore.

It creates WorkspaceOverviewSnapshot containing:

- Data.
- Freshness timestamp.
- Loading status.
- Partial errors.
- Available actions.

Overview does not execute Git, scan ports, read log files, open databases, or independently count project files.

### 11.11 Search and Command Palette

Create shared CommandDescriptor objects used by:

- Buttons.
- Menus.
- Shortcuts.
- Context menus.
- Ctrl+K command palette.

Providers:

- Command.
- Workspace.
- Service.
- Note.
- Database.
- Settings.
- Filename-only file search in early versions.

Search must cancel superseded provider work and preserve keyboard-only navigation.

### 11.12 Settings

Sections:

- General.
- Appearance.
- Account and Security.
- Git.
- Terminal.
- Workspaces.
- Services.
- Logs.
- Database.
- Notifications.
- Advanced.
- About.

Settings are typed and include:

- Key.
- Scope.
- Default.
- Validator.
- Restart requirement.
- Sensitivity.

Separate application, workspace, session, and sensitive settings.

### 11.13 Notifications

Support:

- Toasts.
- Notification center.
- Severity.
- Timestamp.
- Workspace association.
- Actions.
- Deduplication.
- Rate limiting.
- Read/unread state.

Do not implement native Windows notifications in the MVP.

## 12. Accessibility

Minimum requirements:

- Logical tab order.
- Complete keyboard navigation for primary flows.
- Visible focus ring.
- Accessible names and descriptions.
- Labels associated with form controls.
- Status not conveyed by color alone.
- Adequate contrast.
- Screen-reader-friendly table headers.
- Scalable text and layouts.
- Reduced-motion setting.
- No focus traps except intentional modal dialogs.

Test account creation, sign-in, unlock, workspace switching, and destructive confirmations without a mouse.

## 13. Performance budgets

Targets:

| Operation | Initial target |
|---|---|
| Startup to Sign In/Create Account | under 2 seconds |
| Successful sign-in to usable shell | under 1 second without required scan |
| Idle memory | preferably below 200 MB |
| Normal Git status | under 500 ms |
| Log ingestion | several thousand lines/second without GUI stalls |
| SQLite page | 100-500 rows |
| Workspace scan | asynchronous and bounded |

Measure before optimizing. Caches require explicit size and invalidation policies.

Never eagerly load:

- Full Git history.
- Full large diff.
- Entire log file.
- Entire large database table.
- Every workspace file.
- All note bodies at startup.
- Detailed state for inactive workspaces.

## 14. Testing strategy

### 14.1 Unit tests

- Account validation.
- Username normalization.
- Authentication state transitions.
- Password-hasher adapter with test parameters.
- Workspace path equality.
- Detector matching.
- Git parsers.
- Service restart policy.
- Port association.
- Log chunk decoding.
- SQL identifier quoting.
- Pagination.
- Note metadata.
- Settings validation.
- Error redaction.
- Overview composition.

### 14.2 Integration tests

- Account creation and hash persistence.
- Wrong-password and rehash flows.
- Migrations.
- Temporary workspaces.
- Temporary Git repositories.
- QProcess lifecycle helper programs.
- TCP and UDP listener fixtures.
- Log write, truncation, and rotation.
- Temporary SQLite databases.
- Note autosave and crash recovery.
- Recycle Bin behavior where testable.

### 14.3 UI tests

- Create Account.
- Sign In.
- Keep session semantics.
- Manual Lock and password-only Unlock.
- Sign Out.
- Add Workspace.
- Workspace switching.
- Main navigation.
- Command palette.
- Destructive confirmations.
- Keyboard navigation.
- Focus restoration.

### 14.4 Test isolation

- Use QTemporaryDir.
- Use repository-local Git identity.
- Use isolated Git config and environment.
- Bind ephemeral ports.
- Never use the developer's actual workspaces.
- Never assume a particular database or service is installed.
- Skip environment-specific permission tests with a clear reason.

## 15. Packaging and release

Initial packaging:

- Qt deployment through CMake deployment support or windeployqt.
- Inno Setup per-user installer.
- asInvoker manifest.
- No administrator requirement.
- No auto-updater.
- Preserve Local AppData during normal upgrade.
- User-data deletion is an explicit uninstall choice.
- Code-signing hooks exist even if no certificate is currently available.
- Include third-party license notices.
- Test on a clean Windows 11 VM.

Manual releases must include:

- Versioned installer.
- SHA-256 checksum.
- Release notes.
- Known limitations.
- Migration notes.
- Dependency license notice.

## 16. Crash recovery and diagnostics

Enable local application logging by default with:

- Timestamp.
- Severity.
- Category.
- Thread identifier.
- Correlation identifier.
- Rotating size/retention policy.

Never intentionally log:

- Passwords.
- Password hashes.
- Git credentials.
- Tokens.
- Secret environment values.
- Database row content.
- Private note content.

Recovery:

- Detect unclean shutdown.
- Restore window geometry safely.
- Restore selected workspace/page conservatively.
- Recover note journals.
- Observe previously running service processes without automatically adopting or killing them.
- Never automatically rerun a mutating Git or SQL operation.

Support bundle generation is manual and previews included categories before saving.

## 17. Version scope

### MVP / v0.1

- Build, CI, logging, migrations.
- Create Account, Sign In, Lock, Unlock, Sign Out.
- Workspace registry and bounded detection.
- Main shell matching the approved layout.
- Overview composition.
- Read-only Git status/history/diff.
- Basic service Start/Stop/Restart and output.
- Read-only Port Monitor.
- Bounded Log Viewer.
- Read-only SQLite browsing and SQL query execution with warnings.
- Markdown notes.
- Read-only file preview and external open.
- Settings and internal notifications.

### v0.2

- Git stage, unstage, commit, fetch, pull, push, checkout.
- Branch and stash basics.
- Improved service groups and restart policies.
- Port/workspace association improvements.
- Advanced log filters and rotation.
- Command palette.
- Missing workspace relocation.
- Installer preview builds.

### v0.3

- Rich database schema browsing.
- Query cancellation investigation.
- Notification center.
- Git conflict presentation.
- Notes folders/tags/search/export.
- Crash recovery and support bundle.

### v0.5

- SQLite row editing where stable identity exists.
- Transactions and backups before risky changes.
- Advanced branches, merge, stash, tags, and remotes.
- Native Windows process-tree launcher.
- Performance hardening.

### v1.0

- Complete primary scope.
- Accessibility pass.
- Security review.
- Migration and upgrade verification.
- Signed build support even if actual public signing remains optional.
- Clean install/uninstall tests.
- Long-running soak tests.

### Future

- Encrypted DevHub-owned secrets.
- Optional encrypted notes.
- Windows Hello or DPAPI-assisted quick unlock with explicit security semantics.
- Docker and Compose.
- REST client.
- PostgreSQL and Redis.
- SSH.
- Native Windows notifications.
- Linux platform providers.
- Public plugin SDK only after ABI, isolation, signing, and permission design.

# Complete Implementation Roadmap

The sequence below is dependency-aware. A task should not be started merely because its module is interesting; prerequisites and acceptance tests should already exist.

## Phase 0 — Product, toolchain, and repository foundation

1. Confirm that this document is the current source of truth.
2. Add an architecture-decision-record template under docs/decisions.
3. Record the Qt Widgets decision.
4. Record the Git CLI decision.
5. Record the QSQLITE-first decision.
6. Record the local-account security boundary.
7. Record the Windows 11 x64-only v1 platform scope.
8. Confirm that all selected dependency licenses fit an open-source pet project.
9. Select the exact Qt 6.8 patch release used in development and CI.
10. Select the exact MSVC 2022 toolset version used in CI.
11. Create the root CMakeLists.txt.
12. Require CMake at a version compatible with the selected Qt release.
13. Set the project language to CXX.
14. Require C++20 and disable compiler extensions.
15. Add CMakePresets.json.
16. Add a Ninja Multi-Config developer preset.
17. Add Debug, RelWithDebInfo, and Release build presets.
18. Add a Windows CI configure preset.
19. Add an option controlling test builds.
20. Add an option controlling packaging support.
21. Add an option controlling sanitizer-compatible builds.
22. Create src, tests, resources, docs, cmake, and packaging directories.
23. Create the devhub executable target with a placeholder main.
24. Create a shared project-options interface target.
25. Create a shared project-warnings interface target.
26. Enable AUTOMOC, AUTOUIC, and AUTORCC where needed.
27. Locate Qt Core, Gui, Widgets, Sql, Concurrent, and Test targets.
28. Link only required Qt modules to each target.
29. Configure high MSVC warning levels for project code.
30. Enable warnings as errors in CI.
31. Keep third-party headers and targets outside project warning policy.
32. Add clang-format configuration.
33. Add a format-check command.
34. Add a curated clang-tidy configuration.
35. Add a static-analysis build preset.
36. Create a vcpkg manifest.
37. Add libsodium to the manifest.
38. Pin a vcpkg baseline.
39. Document dependency update procedure.
40. Create a third-party license inventory.
41. Configure CTest.
42. Create the first Qt Test executable.
43. Add a smoke test that starts QCoreApplication.
44. Configure test output on failure.
45. Add application organization, name, and version constants.
46. Add Windows version-resource generation.
47. Create the initial Qt resource collection.
48. Add placeholder application and tray-quality icons.
49. Document build, test, format, and analysis commands.
50. Add a CI workflow that configures, builds, and tests.

## Phase 1 — Shared core and application bootstrap

51. Define the DevHub C++ namespace policy.
52. Create ApplicationPaths using QStandardPaths.
53. Resolve Local AppData and optional roaming configuration paths.
54. Create required data, log, cache, backup, and user directories.
55. Validate directory writability during startup.
56. Define StartupFailure and StartupRecoveryAction.
57. Create ApplicationBootstrapper.
58. Move all startup construction out of main.cpp.
59. Create an injectable ApplicationClock interface.
60. Implement SystemApplicationClock.
61. Create an injectable IdGenerator interface.
62. Implement UUID-based IdGenerator.
63. Define ErrorCategory.
64. Define stable ErrorCode values.
65. Define AppError.
66. Define the project Result convention.
67. Represent cancellation separately from failure.
68. Create ErrorPresenter.
69. Create TechnicalDetailsFormatter.
70. Define CorrelationId.
71. Propagate correlation IDs through background operations.
72. Create RedactionService.
73. Add redaction for password-related fields.
74. Add redaction for tokens and authorization headers.
75. Add redaction for credential-bearing URLs.
76. Add redaction for configured secret environment names.
77. Create Notification value types.
78. Create NotificationService without widget dependencies.
79. Add notification deduplication keys.
80. Add notification rate-limiting policy.
81. Create ApplicationEventHub with typed Qt signals.
82. Prohibit string event names and QVariantMap event payloads.
83. Define TaskId and TaskGeneration.
84. Define CancellationSource and CancellationToken.
85. Create TaskScheduler around a bounded QThreadPool.
86. Add per-module concurrency limits.
87. Add workspace ownership to scheduled tasks.
88. Add stale-generation result rejection.
89. Create reusable debounce support.
90. Create reusable throttle support.
91. Add a clean application-shutdown coordinator.
92. Define application lifecycle states.
93. Add startup timing instrumentation.
94. Add safe fatal-startup error logging.
95. Create a recovery-mode shell for fatal bootstrap failures.

## Phase 2 — Theme and application shell

96. Define semantic ColorToken values.
97. Define spacing, radius, size, and typography tokens.
98. Create ThemePalette for the dark theme.
99. Create ThemeManager as an application-owned dependency.
100. Create a minimal QProxyStyle for shared control behavior.
101. Apply the dark QPalette globally.
102. Create a small scoped stylesheet layer for custom properties.
103. Prohibit raw color literals in feature widgets.
104. Create SVG icon loading and tint caching.
105. Select or create an open-source icon set and record its license.
106. Create AppLogoWidget using the approved brand direction.
107. Create AccentButton.
108. Create SecondaryButton.
109. Create DestructiveButton.
110. Create IconButton.
111. Create SearchField.
112. Create PasswordField with visibility toggle.
113. Create FormField with label, help, required state, and error state.
114. Create CardWidget.
115. Create StatusBadge that includes text or shape, not color alone.
116. Create EmptyStateWidget.
117. Create InlineErrorWidget.
118. Create LoadingOverlay.
119. Create ConfirmationDialog.
120. Create TechnicalDetailsPanel.
121. Create MainWindow with a standard Windows frame.
122. Create the three-region main layout.
123. Create WorkspaceSidebar placeholder.
124. Create ModuleNavigation placeholder.
125. Create the main QStackedWidget page host.
126. Create the workspace header.
127. Add toolbar actions for terminal, folder, settings, and overflow.
128. Make header actions collapse into overflow at narrow widths.
129. Implement safe window-geometry persistence.
130. Recover windows whose saved geometry is off-screen.
131. Define the practical minimum window size.
132. Add high-DPI icon and layout verification.
133. Add visible focus states to all reusable controls.
134. Add accessible names to icon-only buttons.
135. Create the geometric dark background painter for authentication.
136. Cache the authentication background at each relevant size/DPI.
137. Avoid continuous repaint or animation of the background.
138. Create a reduced-motion setting hook.
139. Add a basic visual-regression screenshot harness.
140. Verify the shell at 100%, 125%, 150%, and 200% scale.

## Phase 3 — Internal database and migrations

141. Define DatabaseConnectionName.
142. Create InternalDatabaseManager.
143. Open the internal QSQLITE database at the Local AppData path.
144. Give every QSqlDatabase connection a unique explicit name.
145. Ensure connection creation and removal occur in the owning thread.
146. Enable SQLite foreign keys on every internal connection.
147. Configure a bounded busy timeout.
148. Evaluate and enable WAL for the internal database.
149. Create SqlTransactionGuard.
150. Require explicit commit.
151. Roll back uncommitted transactions.
152. Create SqlErrorMapper.
153. Map QSqlError categories into AppError.
154. Create the schema_migrations table.
155. Define the migration file/resource naming convention.
156. Implement ordered migration discovery.
157. Implement migration transaction handling.
158. Record successful migration version and timestamp.
159. Reject duplicate migration versions.
160. Refuse databases newer than the application.
161. Create pre-migration backups for designated risky migrations.
162. Define backup retention.
163. Create the initial accounts table.
164. Create the initial application_settings table.
165. Create the initial workspaces table.
166. Create the initial workspace_settings table.
167. Create the initial workspace_detections table.
168. Create the initial service_definitions table.
169. Create the initial log_sources table.
170. Create the initial database_references table.
171. Create the initial note metadata tables.
172. Create the initial notifications table.
173. Create the initial ui_state table.
174. Add indexes for actual query patterns.
175. Avoid speculative indexes without measured queries.
176. Add a clean-database migration integration test.
177. Add an already-current database test.
178. Add a migration rollback test.
179. Add a newer-schema refusal test.
180. Add a corrupted-database startup test.
181. Create a database recovery screen action to copy aside the file.
182. Never silently delete or recreate an incompatible database.

## Phase 4 — Account persistence and password hashing

183. Define AccountId.
184. Define Account entity without a plaintext password field.
185. Define normalized and display username value objects.
186. Set a documented username length limit.
187. Reject control characters in usernames.
188. Reject path separators in usernames.
189. Define case-folded username comparison behavior.
190. Create IAccountRepository.
191. Implement SqlAccountRepository.
192. Enforce the single-account v1 rule in the repository and schema.
193. Store created_at, updated_at, and last_login_at.
194. Store the encoded password hash.
195. Store a password scheme version.
196. Create PasswordHasher interface.
197. Implement SodiumPasswordHasher.
198. Initialize libsodium during bootstrap.
199. Fail startup safely when libsodium initialization fails.
200. Configure production Argon2id parameter policy.
201. Configure fast, isolated parameters for automated tests.
202. Hash passwords only on a background worker.
203. Verify passwords only on a background worker.
204. Disable duplicate authentication submissions during hashing.
205. Convert password text into a short-lived byte buffer.
206. Clear controllable password buffers after use.
207. Never copy password values into AppError.
208. Never include encoded hashes in diagnostics.
209. Implement needsRehash.
210. Rehash after a successful sign-in when policy changes.
211. Persist rehash and last-login updates transactionally.
212. Add known-good hash/verify tests.
213. Add wrong-password tests.
214. Add corrupted-hash tests.
215. Add needs-rehash tests.
216. Add username normalization tests.
217. Add repository single-account constraint tests.
218. Measure Argon2id duration on representative hardware.
219. Document the chosen parameter profile and rationale.
220. Confirm that startup never hashes or verifies on the GUI thread.

## Phase 5 — Authentication UI and session lifecycle

221. Define SessionState enum.
222. Create SessionController.
223. Create AuthenticationService.
224. Create InactivityMonitor.
225. Define account-creation input validation results.
226. Define sign-in input validation results.
227. Define unlock input validation results.
228. Create AuthenticationWindow.
229. Create BrandHeaderWidget for authentication pages.
230. Create CreateAccountWidget.
231. Add Username, Password, and Confirm Password fields.
232. Add the Create Account primary action.
233. Add the local-only security explanation.
234. Add the statement describing what login does not encrypt.
235. Validate required fields before starting a hash.
236. Validate password confirmation without persisting either value.
237. Show password guidance without arbitrary composition rules.
238. Permit paste into password fields.
239. Add password visibility toggles with accessible labels.
240. Implement atomic account-and-user-directory creation.
241. Roll back account creation if required initialization fails.
242. Sign in the new account after successful creation.
243. Create SignInWidget.
244. Display Welcome back.
245. Prefill the stored account username.
246. Keep the username editable.
247. Focus the password field when the prefilled username is valid.
248. Add Keep me signed in for this session.
249. Default the checkbox according to an explicit product setting.
250. Add Sign In and Quit actions.
251. Submit on Enter.
252. Use one invalid-credentials message.
253. Clear the password after a failed attempt.
254. Add bounded local failed-attempt delay.
255. Keep the window responsive during the delay.
256. Update last_login_at only after successful verification.
257. Create UnlockWidget.
258. Display the signed-in username as read-only context.
259. Ask only for the password.
260. Add Unlock, Sign Out, and Quit actions.
261. Implement manual Lock from the main window.
262. Hide sensitive pages before exposing the lock view.
263. Preserve the selected workspace and page during Lock.
264. Restore focus safely after Unlock.
265. Implement Sign Out as session destruction plus full Sign In.
266. Clear session memory at application exit.
267. Ensure Keep session never creates a persistent token.
268. Suppress inactivity locking when Keep session is checked.
269. Apply inactivity policy when Keep session is unchecked.
270. Reset inactivity timing on relevant local input.
271. Do not count background log/service activity as user activity.
272. Ensure manual Lock always overrides Keep session.
273. Leave managed services running during Lock.
274. Keep bounded service output collection during Lock.
275. Create ChangePasswordDialog.
276. Require the current password.
277. Validate new password and confirmation.
278. Replace the hash transactionally.
279. Keep the current session after a successful change.
280. Create ForgotPasswordInformationDialog.
281. Explain local-only recovery limitations.
282. Create the typed-confirmation profile-reset flow.
283. Rename the old profile directory instead of deleting it.
284. Verify that profile reset never touches workspace directories.
285. Return to Create Account after a successful reset.
286. Add Create Account UI tests.
287. Add Sign In success and failure UI tests.
288. Add Keep session semantic tests.
289. Add manual Lock and password-only Unlock tests.
290. Add Sign Out tests.
291. Add inactivity-lock tests with a fake clock.
292. Add password-change integration tests.
293. Add profile-reset containment tests.
294. Measure startup-to-authentication-screen performance.
295. Meet the under-two-second target on representative hardware.

## Phase 6 — Workspace domain and persistence

296. Define WorkspaceId.
297. Define WorkspacePath.
298. Define WorkspaceStatus.
299. Define Workspace aggregate.
300. Store display name separately from directory name.
301. Store entered path and canonical path where useful.
302. Define Windows case-insensitive path equality.
303. Create IWorkspaceRepository.
304. Implement SqlWorkspaceRepository.
305. Scope workspace rows to AccountId even with one v1 account.
306. Create WorkspaceValidator.
307. Validate that the selected path exists.
308. Validate that it is a directory.
309. Validate basic read access.
310. Distinguish missing from inaccessible.
311. Detect duplicate canonical roots.
312. Create WorkspaceManager.
313. Implement Add Workspace.
314. Commit the registry record before scheduling nonessential scans.
315. Implement Rename Workspace.
316. Implement Pin and Unpin.
317. Implement Remove Workspace metadata.
318. Prohibit removal from deleting project files.
319. Implement recent-workspace ordering.
320. Update lastOpenedAt only after successful activation.
321. Create WorkspaceStateStore.
322. Create WorkspaceListModel.
323. Define model roles for name, path, status, favorite, and recency.
324. Create WorkspaceListDelegate matching the visual reference.
325. Add workspace search by name and path.
326. Preserve selection through model refresh.
327. Create AddWorkspaceDialog.
328. Add folder-picker registration.
329. Add folder drag-and-drop registration.
330. Show an Open action when a selected folder is already registered.
331. Create WorkspaceContext for the active workspace.
332. Cancel obsolete workspace-scoped page jobs on switch.
333. Create WorkspaceRelocationService.
334. Detect missing workspaces at startup without blocking auth.
335. Show missing and inaccessible status indicators.
336. Let the user locate a moved workspace.
337. Revalidate the replacement directory.
338. Prevent relocation onto another registered workspace.
339. Add Open in Explorer.
340. Add Copy Absolute Path.
341. Add Copy Relative Path where context exists.
342. Connect Open Terminal through ITerminalLauncher.
343. Add workspace repository tests.
344. Add duplicate-path/case tests.
345. Add Unicode and whitespace path tests.
346. Add long-path tests.
347. Add disconnected-drive tests where feasible.
348. Add best-effort UNC path tests.
349. Add workspace-switch cancellation tests.
350. Verify that no workspace scan starts before authentication.

## Phase 7 — Bounded workspace detection

351. Define DetectorId.
352. Define DetectionEvidence.
353. Define DetectionResult.
354. Define confidence levels.
355. Create IWorkspaceDetector.
356. Create WorkspaceDetectorRegistry.
357. Define detector ordering.
358. Define duplicate-result merging.
359. Create ScanBudget.
360. Set default maximum depth to 12.
361. Set default inspected-file maximum to 100,000.
362. Add a configurable wall-time budget.
363. Create the default ignored-directory set.
364. Ignore .git contents while still detecting the marker.
365. Ignore node_modules.
366. Ignore build, dist, out, and target.
367. Ignore .vs and IDE cache directories.
368. Detect common CMake build-directory patterns.
369. Prevent traversal through directory cycles.
370. Treat symlinks and junctions cautiously.
371. Create WorkspaceScanner.
372. Run scanning through TaskScheduler.
373. Report throttled scan progress.
374. Support cancellation.
375. Stop cleanly when the workspace disappears.
376. Persist completed scan results atomically.
377. Version detector output.
378. Invalidate results when detector version changes.
379. Create Git marker detector.
380. Support .git directory and .git file markers.
381. Create CMake detector.
382. Create package.json detector.
383. Create go.mod detector.
384. Create Cargo.toml detector.
385. Create Visual Studio solution/project detector.
386. Create Dockerfile detector.
387. Create Compose filename detector.
388. Create Makefile detector.
389. Create Python marker detector without invoking Python.
390. Create README detector.
391. Create SQLite extension detector.
392. Add optional SQLite header validation.
393. Create log-file candidate detector.
394. Store evidence as workspace-relative paths.
395. Avoid opening large candidate log files during detection.
396. Add detector unit fixtures.
397. Add scan-limit tests.
398. Add directory-cycle tests.
399. Add disappearing-entry tests.
400. Add scan performance tests against a generated large tree.

## Phase 8 — Git executable and read-only client

401. Define GitVersion and version comparison.
402. Create GitExecutableLocator.
403. Check the configured executable path first.
404. Fall back to PATH discovery.
405. Validate candidates with git --version.
406. Reject Git versions older than 2.40 with a clear message.
407. Store the selected executable path in settings.
408. Add a Git path picker and validation action.
409. Define GitCommand.
410. Include operation name, arguments, directory, timeout, and mutation flag.
411. Define GitCommandResult.
412. Preserve stdout, stderr, exit code, exit status, duration, and truncation.
413. Create GitCommandRunner around QProcess.
414. Pass program and arguments separately.
415. Set the repository working directory explicitly.
416. Add --no-pager where supported.
417. Disable color output.
418. Prevent terminal credential prompts for background commands.
419. Capture stdout and stderr independently.
420. Enforce the 16 MB normal-output limit.
421. Enforce the 8 MB diff-preview limit.
422. Mark and explain truncated output.
423. Add command timeouts.
424. Add cancellation with terminate-then-kill escalation.
425. Redact credentials and sensitive URLs in diagnostics.
426. Log operation metadata without arbitrary command secrets.
427. Create GitRepositoryLocator.
428. Use Git to resolve repository root.
429. Support .git worktree files.
430. Detect non-repositories.
431. Define GitRepositoryState.
432. Define GitFileStatus.
433. Define GitBranchState.
434. Define GitOperationState.
435. Create GitStatusParser.
436. Use porcelain version 2 with NUL termination.
437. Parse branch OID.
438. Parse current branch and detached state.
439. Parse upstream.
440. Parse ahead and behind.
441. Parse ordinary changed entries.
442. Parse staged and unstaged columns.
443. Parse rename and copy entries.
444. Parse unmerged entries.
445. Parse untracked entries.
446. Handle unusual filenames without line-based assumptions.
447. Create GitService.
448. Implement repository status.
449. Implement current branch.
450. Implement local branch list with explicit formatting.
451. Implement remote branch list.
452. Implement remote list with redacted URLs.
453. Define GitCommit.
454. Create GitHistoryParser.
455. Use unambiguous field and record separators.
456. Implement paged commit history.
457. Implement commit details.
458. Implement file history as a later read operation.
459. Create GitDiffParser.
460. Implement working-tree diff preview.
461. Implement staged diff preview.
462. Implement commit diff preview.
463. Detect binary diff output.
464. Create GitStateStore.
465. Add freshness and loading state.
466. Add per-operation error state.
467. Coalesce status refresh requests.
468. Reject stale status generations.
469. Create GitStatusModel.
470. Group staged, unstaged, untracked, and conflicted files.
471. Create GitHistoryModel.
472. Add incremental page insertion.
473. Create GitBranchModel.
474. Build GitPage with status, history, branches, and diff regions.
475. Preserve selection when compatible state refreshes.
476. Add retry and cancel affordances.
477. Add a missing-Git empty state.
478. Add an unsupported-version empty state.
479. Add temporary-repository test helpers.
480. Isolate test HOME and Git configuration.
481. Configure repository-local test identity.
482. Test clean and dirty status.
483. Test Unicode and whitespace filenames.
484. Test renames.
485. Test merge conflicts.
486. Test detached HEAD.
487. Test unborn branches.
488. Test repository worktrees.
489. Test history pagination.
490. Test diff truncation.
491. Test timeout and cancellation.
492. Test credential prompt suppression.
493. Measure normal repository status against the 500 ms target.
494. Feed GitStateStore into OverviewStateComposer.
495. Add View in Git navigation from the Overview card.

## Phase 9 — Incremental Git mutations

496. Create a per-repository GitOperationQueue.
497. Serialize repository mutations.
498. Prevent incompatible actions during a mutation.
499. Show the current operation in GitPage.
500. Implement Stage selected paths.
501. Put a double dash before staged paths.
502. Implement Stage All as a separate action.
503. Implement Unstage selected paths.
504. Choose version-compatible restore/reset behavior.
505. Refresh status after stage and unstage.
506. Create CommitDialog.
507. Validate a nonempty commit summary according to product policy.
508. Pass the commit message without shell interpolation.
509. Surface hook and signing failures.
510. Refresh status and history after commit.
511. Implement Fetch with remote selection.
512. Classify common network and authentication failures.
513. Read repository pull strategy configuration.
514. Display merge, rebase, fast-forward-only, or unspecified.
515. Create PullDialog when strategy is ambiguous.
516. Never silently choose a conflicting pull strategy.
517. Implement Pull using explicit or repository configuration.
518. Detect conflicts after Pull.
519. Implement Push.
520. Implement first-push upstream selection.
521. Create Checkout action.
522. Refuse checkout when Git reports unsafe local changes.
523. Create branch from an explicit start point.
524. Rename a local branch.
525. Delete a local branch with confirmation.
526. Prevent deletion of the checked-out branch.
527. Do not add forced branch deletion in the early release.
528. Implement Stash Create with optional message.
529. Implement stable Stash List parsing.
530. Implement Stash Apply.
531. Implement Stash Pop as a distinct action.
532. Warn that Pop may partially apply and conflict.
533. Implement Merge with preflight status.
534. Present conflict state without automatic resolution.
535. Implement Merge Abort only during a detected merge.
536. Defer force push.
537. Defer hard reset.
538. Defer aggressive clean.
539. Add confirmation for every destructive Git action.
540. Add mutation audit messages without sensitive content.
541. Add stage and unstage integration tests.
542. Add commit integration tests.
543. Add hook-failure tests.
544. Add fetch/pull/push tests using a local bare remote.
545. Add checkout and branch tests.
546. Add stash tests.
547. Add merge conflict and abort tests.
548. Add mutation cancellation tests where safe.
549. Verify status refresh after failure and partial success.
550. Register Git actions with CommandRegistry.

## Phase 10 — Service definitions and process lifecycle

551. Define ServiceId.
552. Define ServiceLifecycleState.
553. Define ServiceHealthState.
554. Define RestartPolicy.
555. Define StopPolicy.
556. Define ServiceDefinition.
557. Keep program and argument list separate.
558. Add an explicit shellMode boolean.
559. Define expected-port entries.
560. Define environment-reference entries.
561. Create IServiceDefinitionRepository.
562. Implement SqlServiceDefinitionRepository.
563. Scope definitions to workspace and account.
564. Validate unique service names within a workspace.
565. Validate program presence.
566. Validate argument count and total size.
567. Resolve relative working directories against the workspace.
568. Validate working-directory existence before start.
569. Validate environment variable names.
570. Support references to the current process environment.
571. Do not persist secret values in MVP.
572. Create ServiceDefinitionEditor.
573. Make direct executable mode the default.
574. Add a visible warning for shell mode.
575. Preview effective program, arguments, and working directory.
576. Display environment names without secret values.
577. Create IManagedProcess.
578. Create IProcessLauncher.
579. Implement QProcessManagedProcess.
580. Implement QProcessLauncher.
581. Create a fresh QProcessEnvironment for each launch.
582. Avoid mutating the global process environment.
583. Capture stdout and stderr separately.
584. Capture process ID.
585. Capture start time.
586. Map QProcess errors into Process AppErrors.
587. Create ManagedService.
588. Create ServiceCoordinator.
589. Create ServiceStateStore.
590. Create ServiceListModel.
591. Implement Start.
592. Transition through Starting.
593. Detect start timeout.
594. Transition to Running only after process start confirmation.
595. Implement graceful Stop.
596. Add a configurable graceful-stop timeout.
597. Escalate to forced termination only according to policy.
598. Implement Restart as stop then start.
599. Prevent overlapping duplicate instances.
600. Implement Start All with bounded concurrency.
601. Implement Stop All in reverse deterministic start order.
602. Implement Restart All without overlapping instances.
603. Detect unexpected exit.
604. Capture exit code and exit status.
605. Create RestartPolicyEvaluator.
606. Implement Never restart.
607. Implement On Failure restart.
608. Implement Always restart.
609. Add exponential backoff.
610. Add a maximum retry count and time window.
611. Detect restart loops and transition to Failed.
612. Route process output to ServiceOutputRouter.
613. Publish service-output chunks to the log subsystem.
614. Associate managed PID with workspace and service.
615. Check expected ports asynchronously after start.
616. Distinguish Running from Healthy.
617. Preserve service runtime state during UI lock.
618. Keep service processes running during inactivity lock.
619. Add the three-choice exit prompt when services run.
620. Implement Stop Services and Exit.
621. Implement Leave Services Running and Exit.
622. Explain that supervision and capture end when DevHub exits.
623. Implement Cancel exit.
624. Avoid persisting PID as authoritative running state.
625. Observe possible previous service processes after an unclean shutdown.
626. Do not automatically adopt or kill them.
627. Design WindowsManagedProcessLauncher.
628. Plan CreateProcessW suspended creation.
629. Plan anonymous or overlapped output pipes.
630. Plan Job Object assignment before process resume.
631. Plan kill-on-job-close policy only for owned trees.
632. Implement the native launcher in v0.5 after MVP validation.
633. Create helper executable that emits stdout and stderr.
634. Create helper executable that exits with a chosen code.
635. Create helper executable that spawns a child.
636. Create helper executable that ignores graceful stop.
637. Create helper executable that floods output.
638. Add lifecycle transition unit tests.
639. Add direct-launch integration tests.
640. Add shell-mode tests with explicit fixtures.
641. Add environment-reference tests.
642. Add start-timeout tests.
643. Add unexpected-exit tests.
644. Add restart-backoff tests with a fake clock.
645. Add shutdown-choice tests.
646. Register service actions with CommandRegistry.
647. Feed ServiceStateStore into OverviewStateComposer.

## Phase 11 — Windows Port Monitor

648. Define PortProtocol.
649. Define AddressFamily.
650. Define TcpState.
651. Define PortRecord.
652. Define ProcessIdentity using PID and creation time.
653. Create IPortProvider.
654. Implement WindowsPortProvider.
655. Add the two-call buffer sizing pattern for IP Helper APIs.
656. Retry boundedly when table size changes between calls.
657. Enumerate IPv4 TCP owner-PID rows.
658. Enumerate IPv6 TCP owner-PID rows.
659. Enumerate IPv4 UDP owner-PID rows.
660. Enumerate IPv6 UDP owner-PID rows.
661. Convert ports from network byte order.
662. Convert addresses into normalized display values.
663. Map Windows TCP states.
664. Create IProcessInspector.
665. Implement WindowsProcessInspector.
666. Open processes with least required query rights.
667. Resolve executable path where permitted.
668. Resolve process name.
669. Resolve creation time.
670. Tolerate process exit during inspection.
671. Tolerate access denied.
672. Avoid failing the complete refresh for one inaccessible process.
673. Create PortMonitorService.
674. Run enumeration off the GUI thread.
675. Prevent overlapping refreshes.
676. Support manual Refresh.
677. Support configurable auto-refresh.
678. Pause auto-refresh when no consumer needs live state if appropriate.
679. Create PortStateStore.
680. Create PortTableModel.
681. Define columns for port, protocol, PID, process, address, state, and workspace.
682. Add QSortFilterProxyModel for the bounded snapshot.
683. Add TCP/UDP filter.
684. Add listening/all-state filter.
685. Add process and port search.
686. Create PortAssociationService.
687. Associate managed-service PIDs as strongest evidence.
688. Add executable/working-directory evidence when reliably available.
689. Treat expected ports as secondary evidence.
690. Refuse name-only association.
691. Leave uncertain records unassociated.
692. Add Copy Port.
693. Add Copy PID.
694. Add Open Associated Workspace.
695. Create TerminateProcessUseCase.
696. Refresh and revalidate process identity before termination.
697. Show process, PID, port, and risk in confirmation.
698. Avoid protected and system process termination.
699. Report access denied without global elevation.
700. Add ephemeral TCP listener integration tests.
701. Add IPv6 listener tests where CI supports them.
702. Add UDP listener tests.
703. Add PID-disappearance race tests.
704. Add access-denied record tests with fakes.
705. Add association-policy unit tests.
706. Add refresh performance tests.
707. Register port actions with CommandRegistry.
708. Feed PortStateStore into Services and Overview.

## Phase 12 — Log subsystem

709. Define LogSourceId.
710. Define LogSourceKind.
711. Define LogSeverity.
712. Define LogLine sequence identity.
713. Define LogSource configuration.
714. Create ILogSourceRepository.
715. Implement SqlLogSourceRepository.
716. Create LogRingBuffer.
717. Set the default line cap to 100,000 per active source.
718. Add a configurable byte cap.
719. Evict oldest lines when either cap is exceeded.
720. Preserve monotonically increasing sequence numbers.
721. Support batched append.
722. Create LogLineModel.
723. Apply model changes only on the GUI thread.
724. Batch begin/end insert operations.
725. Create FileIdentity.
726. Add Windows file identity support where available.
727. Create LogDecoder.
728. Implement UTF-8.
729. Implement UTF-8 BOM handling.
730. Implement ASCII.
731. Preserve incomplete multibyte sequences.
732. Create FileLogReader.
733. Track a qint64 byte offset.
734. Read fixed-size chunks.
735. Preserve a partial trailing line.
736. Define initial-open tail policy for large files.
737. Use QFileSystemWatcher as a change hint.
738. Add polling fallback.
739. Re-register a watcher after file replacement.
740. Detect truncation when size becomes smaller than offset.
741. Detect rotation through file identity.
742. Handle copy-truncate behavior.
743. Define behavior when a rotated file disappears.
744. Add Pause display.
745. Continue bounded source tracking while paused.
746. Add Resume with controlled catch-up.
747. Add Clear View without modifying the source file.
748. Do not expose source truncation in early versions.
749. Create LogParser.
750. Add configurable severity recognition.
751. Add optional timestamp recognition.
752. Preserve raw text independently of parsed fields.
753. Create LogFilterEngine.
754. Implement substring filtering.
755. Implement regex filtering in background work.
756. Validate regex before applying it.
757. Cancel superseded filter work.
758. Debounce filter edits.
759. Add severity toggles.
760. Add Search Next and Search Previous.
761. Add selected-line copy.
762. Add selected-line export through QSaveFile.
763. Integrate service stdout as a log source.
764. Integrate service stderr as a distinguishable channel.
765. Batch high-frequency service output.
766. Limit UI update frequency.
767. Detect producer backlog.
768. Report dropped or compacted data visibly.
769. Add log-source discovery from workspace detections.
770. Create LogsPage with source list and virtualized content view.
771. Add empty, loading, missing, rotated, and error states.
772. Add decoding tests for chunk boundaries.
773. Add partial-line tests.
774. Add truncation tests.
775. Add rename/rotation tests.
776. Add copy-truncate tests.
777. Add line/byte eviction tests.
778. Add regex cancellation tests.
779. Add several-thousand-lines-per-second soak test.
780. Register log actions with CommandRegistry.
781. Feed recent warning/error summaries into Overview.

## Phase 13 — SQLite discovery, browsing, and queries

782. Define DatabaseId.
783. Define DatabaseReference.
784. Define DatabaseOpenMode.
785. Define DatabaseObjectKind.
786. Define DatabaseValue preserving NULL, integer, real, text, and BLOB.
787. Create IDatabaseReferenceRepository.
788. Implement SqlDatabaseReferenceRepository.
789. Create ISqliteConnectionFactory.
790. Implement QtSqlConnectionFactory.
791. Create a unique named connection per database session.
792. Create each connection in its worker thread.
793. Remove each connection only after queries and handles are destroyed.
794. Create IDatabaseSession.
795. Implement QtSqlDatabaseSession.
796. Open discovered databases read-only by default.
797. Prevent an absent discovered path from creating a new database.
798. Configure a bounded busy timeout.
799. Map QSqlError into structured Database errors.
800. Create database candidate validator.
801. Validate standard SQLite headers where practical.
802. Keep extension-only candidates visibly unverified until opened.
803. Register valid references with the workspace.
804. Do not write to a database during detection.
805. Create IDatabaseSchemaReader.
806. Implement QtSqlSchemaReader.
807. Read tables.
808. Read views.
809. Read indexes.
810. Read triggers.
811. Read schema SQL.
812. Read table columns.
813. Read primary-key order.
814. Read foreign-key metadata.
815. Detect WITHOUT ROWID.
816. Create DatabaseObjectModel.
817. Represent schema objects hierarchically.
818. Avoid computing row counts automatically.
819. Add an explicit row-count action.
820. Run row count on the database worker.
821. Permit cancellation or session abandonment according to QSQLITE capability.
822. Create ITablePageSource.
823. Implement QtSqlTablePageSource.
824. Set default page size within 100-500 rows.
825. Make page size configurable within safe bounds.
826. Determine stable row identity.
827. Prefer primary-key pagination where practical.
828. Use bounded LIMIT/OFFSET fallback.
829. Explain unstable pagination under concurrent external writes.
830. Create DatabaseTableModel.
831. Load only the active page.
832. Represent NULL distinctly.
833. Truncate large text in cells.
834. Represent BLOBs by size and type indicator.
835. Add an explicit BLOB preview with a size cap.
836. Add BLOB export.
837. Implement backend sorting with quoted identifiers.
838. Centralize SQLite identifier quoting.
839. Implement structured value filters using bound parameters.
840. Never bind identifiers as values.
841. Never concatenate user values into generated SQL.
842. Create SQL editor using QPlainTextEdit.
843. Add line numbers.
844. Add lightweight SQL syntax highlighting.
845. Add Run Statement.
846. Add Run Selection.
847. Add Run Script with explicit multi-statement warning.
848. Classify obvious read-only statements conservatively.
849. Treat ambiguous SQL as potentially mutating.
850. Warn before INSERT, UPDATE, DELETE, DDL, PRAGMA writes, or scripts.
851. Prefer automatic backup before schema-changing SQL when practical.
852. Explain that backup does not guarantee recovery.
853. Create IQueryExecutor.
854. Implement QtSqlQueryExecutor.
855. Execute on the session worker thread.
856. Stream or page result rows into bounded memory.
857. Cap columns and retained result bytes.
858. Show duration, affected rows, and truncation.
859. Add a Stop action with documented QSQLITE limitations.
860. Do not block the GUI while waiting for an uninterruptible query.
861. Allow closing/abandoning a stuck session safely where possible.
862. Record a concrete cancellation limitation if direct SQLite becomes necessary.
863. Create in-memory session query history.
864. Set persistent history disabled by default.
865. Add explicit persistent-history opt-in.
866. Set the default persistent limit to 500.
867. Warn that SQL text may contain secrets.
868. Never store result rows in history.
869. Add Clear Query History.
870. Create DatabasePage with schema, data, and query tabs.
871. Add read-only mode badge.
872. Add lock/busy status.
873. Add database close action.
874. Register database actions with CommandRegistry.
875. Add schema reader integration tests.
876. Add pagination tests with large tables.
877. Add mixed-type and NULL tests.
878. Add Unicode text tests.
879. Add BLOB tests.
880. Add composite primary-key tests.
881. Add WITHOUT ROWID tests.
882. Add external lock/busy tests.
883. Add mutation-warning tests.
884. Add query-history default-off tests.
885. Add connection thread-affinity tests.
886. Add leaked-connection detection in test shutdown.
887. Feed database summaries into Overview.

## Phase 14 — SQLite visual editing in v0.5 or later

888. Define explicit DatabaseEditMode.
889. Require deliberate activation per database session.
890. Warn that external application invariants may be bypassed.
891. Offer a backup before enabling risky changes.
892. Restrict grid edits to rows with stable identity.
893. Disable grid edits for ambiguous row identity.
894. Define PendingCellEdit.
895. Define PendingRowInsert.
896. Define PendingRowDelete.
897. Keep edits pending until explicit commit.
898. Create prepared UPDATE statements.
899. Bind every changed value.
900. Include stable row identity in UPDATE predicates.
901. Detect zero or multiple affected rows.
902. Create prepared INSERT statements.
903. Respect omitted/default columns.
904. Create prepared DELETE statements.
905. Preview the row identity before deletion.
906. Wrap a batch in an explicit transaction.
907. Add Commit Changes.
908. Add Roll Back Changes.
909. Refresh the page after commit.
910. Detect external-write conflicts.
911. Never retry writes indefinitely.
912. Add update, insert, and delete integration tests.
913. Add rollback tests.
914. Add trigger side-effect tests.
915. Add stable-identity refusal tests.
916. Reassess whether QSQLITE remains sufficient.

## Phase 15 — Notes

917. Define NoteId.
918. Define NoteNodeKind.
919. Define NoteMetadata.
920. Define NoteTag.
921. Define the per-account/per-workspace notes directory.
922. Generate storage filenames from NoteId rather than title.
923. Prevent title changes from renaming identity unexpectedly.
924. Create INotesMetadataRepository.
925. Implement SqlNotesMetadataRepository.
926. Create NotesRepository combining metadata and Markdown files.
927. Use QSaveFile for note writes.
928. Store a content hash after save.
929. Detect external content changes.
930. Create NotesTreeModel.
931. Add root workspace node.
932. Add folder creation.
933. Add note creation.
934. Add rename.
935. Add movement between folders.
936. Add pin and unpin.
937. Add tags.
938. Add metadata sorting.
939. Add a DevHub notes trash folder.
940. Move deleted notes to recoverable trash.
941. Make permanent delete a separate confirmed action.
942. Create Markdown editor using QPlainTextEdit.
943. Add line numbers if usability testing supports them.
944. Add Markdown syntax highlighting.
945. Add debounced autosave.
946. Flush pending saves before workspace switch.
947. Flush pending saves before Lock.
948. Flush pending saves during orderly shutdown.
949. Create NotesRecoveryJournal.
950. Journal unsaved changes without logging content.
951. Detect a newer recovery journal at startup.
952. Offer Restore, Compare, or Discard.
953. Create Markdown preview with QTextDocument support.
954. Block or control remote resource loading.
955. Add safe local-image handling policy.
956. Add checkbox editing with predictable Markdown modification.
957. Add title search.
958. Add tag search.
959. Add content search on background workers.
960. Cancel superseded searches.
961. Add Export Note.
962. Add Export Folder preserving hierarchy.
963. Add Open Note File Externally.
964. Do not offer in-project storage before v1.
965. Do not encrypt notes by default.
966. Register note actions with CommandRegistry.
967. Add note atomic-save tests.
968. Add autosave debounce tests.
969. Add crash-recovery tests.
970. Add external-modification conflict tests.
971. Add folder and tag repository tests.
972. Add search cancellation tests.
973. Feed pinned and recent notes into Overview.

## Phase 16 — File Browser and external tools

974. Create FileBrowserController.
975. Create a QFileSystemModel for the active workspace.
976. Set the workspace root explicitly.
977. Hide dot entries.
978. Make hidden-file visibility configurable.
979. Avoid recursively preloading the complete tree.
980. Create file-type icon caching.
981. Add Expand and Collapse.
982. Restore a bounded expansion state.
983. Add Copy Absolute Path.
984. Add Copy Workspace-Relative Path.
985. Add Open using the system-associated application.
986. Add Open Containing Folder.
987. Define IExternalEditorLauncher.
988. Add configured IDE/editor profiles.
989. Pass the selected path without shell interpolation.
990. Add read-only text preview.
991. Set a preview file-size limit.
992. Detect likely binary files.
993. Detect UTF-8 BOM.
994. Decode UTF-8 and ASCII.
995. Show unsupported encoding instead of corrupt text.
996. Add Create File.
997. Validate Windows filenames.
998. Add Create Directory.
999. Add Rename.
1000. Check collisions.
1001. Revalidate source and destination before mutation.
1002. Define ITrashService.
1003. Implement WindowsTrashService using a supported shell/Qt mechanism.
1004. Prefer Recycle Bin deletion.
1005. Do not expose ordinary permanent deletion in early releases.
1006. Prevent workspace-root deletion.
1007. Display when a symlink or junction leaves the workspace.
1008. Do not assume a linked target is safe because its visible path is inside.
1009. Refresh only affected tree nodes.
1010. Add context actions through CommandRegistry.
1011. Add filename validation tests.
1012. Add create and rename tests.
1013. Add root-protection tests.
1014. Add symlink/junction boundary tests where feasible.
1015. Add preview size and encoding tests.
1016. Document built-in source editing as out of scope.
1017. Feed recent-file metadata into Overview if useful.

## Phase 17 — Overview and shared actions

1018. Define WorkspaceOverviewSnapshot.
1019. Include per-section freshness.
1020. Include per-section loading state.
1021. Include partial errors.
1022. Include summary actions.
1023. Create OverviewStateComposer.
1024. Subscribe to module state stores.
1025. Debounce burst recomposition.
1026. Do not trigger backend work from recomposition.
1027. Create OverviewPage.
1028. Create Git Status card.
1029. Create Services/Run card.
1030. Create Open Ports card.
1031. Create Recent Logs card.
1032. Create Database card.
1033. Create Notes card.
1034. Match the approved three-column visual hierarchy.
1035. Reflow cards to two columns at narrower widths.
1036. Reflow to one column when necessary.
1037. Display stale-data timestamps.
1038. Display partial failure inside the affected card.
1039. Add View, Manage, Refresh, and Open actions through commands.
1040. Avoid duplicating Git, port, log, or database logic.
1041. Add Overview composition unit tests with fake stores.
1042. Add a test proving Overview does not invoke module backends.
1043. Add loading, empty, stale, and partial-error visual tests.

## Phase 18 — Command registry and global search

1044. Define CommandId.
1045. Define CommandCategory.
1046. Define CommandContext.
1047. Define CommandAvailability.
1048. Define CommandDescriptor.
1049. Create CommandRegistry.
1050. Register navigation commands.
1051. Register account commands.
1052. Register workspace commands.
1053. Register Git commands.
1054. Register service commands.
1055. Register port commands.
1056. Register log commands.
1057. Register database commands.
1058. Register note commands.
1059. Register file commands.
1060. Back reusable commands with QAction where appropriate.
1061. Use the same command from buttons, menus, shortcuts, and palette.
1062. Evaluate availability against current context.
1063. Include an unavailable reason.
1064. Create ICommandSearchProvider.
1065. Create CommandSearchProvider.
1066. Create WorkspaceSearchProvider.
1067. Create ServiceSearchProvider.
1068. Create NoteSearchProvider.
1069. Create DatabaseSearchProvider.
1070. Create SettingsSearchProvider.
1071. Defer full file-content indexing.
1072. Create Ctrl+K CommandPaletteDialog.
1073. Add fuzzy matching.
1074. Rank exact and prefix matches first.
1075. Include recent-command ranking with a bounded history.
1076. Query providers incrementally.
1077. Cancel superseded provider searches.
1078. Preserve keyboard-only selection.
1079. Add Enter execution and Escape dismissal.
1080. Restore focus after palette close.
1081. Add command availability tests.
1082. Add fuzzy-ranking tests.
1083. Add provider cancellation tests.
1084. Add keyboard-flow UI tests.

## Phase 19 — Settings and terminal launch

1085. Define SettingKey.
1086. Define SettingScope.
1087. Define SettingSensitivity.
1088. Define SettingDescriptor.
1089. Create ISettingsRepository.
1090. Implement SqlSettingsRepository.
1091. Create SettingsService.
1092. Merge defaults, global values, and workspace overrides.
1093. Validate values before persistence.
1094. Identify settings requiring restart.
1095. Separate session-only settings.
1096. Do not expose raw database keys in UI.
1097. Create SettingsWindow.
1098. Create General section.
1099. Create Appearance section.
1100. Create Account and Security section.
1101. Create Git section.
1102. Create Terminal section.
1103. Create Workspaces section.
1104. Create Services section.
1105. Create Logs section.
1106. Create Database section.
1107. Create Notifications section.
1108. Create Advanced section.
1109. Create About section.
1110. Add inactivity-lock duration.
1111. Add Keep-session default.
1112. Add Git executable override.
1113. Add workspace scan limits.
1114. Add log line and byte limits.
1115. Add port refresh interval.
1116. Add SQLite page size.
1117. Add query-history opt-in and limit.
1118. Add destructive confirmation preferences only where safe.
1119. Never permit disabling critical security confirmations globally.
1120. Define ITerminalLauncher.
1121. Implement WindowsTerminalLauncher.
1122. Launch Windows Terminal in the workspace directory.
1123. Implement PowerShell launcher.
1124. Implement cmd launcher.
1125. Add WSL as an explicit optional profile.
1126. Add custom executable and argument-template profile.
1127. Validate placeholders and quoting.
1128. Do not invoke a shell unless the selected profile requires it.
1129. Add terminal profile tests.
1130. Register Settings and terminal actions with CommandRegistry.

## Phase 20 — Notifications

1131. Define NotificationId.
1132. Define severity and category.
1133. Define NotificationAction lifetime.
1134. Create INotificationRepository.
1135. Implement SqlNotificationRepository.
1136. Create NotificationModel.
1137. Create ToastManager.
1138. Limit simultaneously visible toasts.
1139. Queue additional toasts.
1140. Deduplicate repeated events.
1141. Aggregate repeated counts.
1142. Rate-limit noisy sources.
1143. Expire actions whose target no longer exists.
1144. Create NotificationCenter.
1145. Add unread/read state.
1146. Add filtering by workspace, severity, and category.
1147. Add Clear Read.
1148. Add retention settings.
1149. Redact technical details before persistence.
1150. Emit service-unexpected-exit notifications.
1151. Emit expected-port conflict notifications.
1152. Emit Git completion/failure notifications where useful.
1153. Emit database-query failure notifications without query values.
1154. Emit workspace-missing notifications.
1155. Avoid success toasts for every routine refresh.
1156. Add deduplication and rate-limit tests.
1157. Add action-expiry tests.
1158. Defer native Windows toast integration.

## Phase 21 — Application logging and support bundle

1159. Install a Qt message handler.
1160. Define application log categories.
1161. Write logs under Local AppData.
1162. Use rotating files.
1163. Set maximum file size.
1164. Set retention count.
1165. Include UTC timestamp.
1166. Include severity.
1167. Include category.
1168. Include thread and correlation ID.
1169. Route all messages through RedactionService.
1170. Prohibit password and password-hash logging.
1171. Prohibit secret environment values.
1172. Prohibit Git credential material.
1173. Prohibit database row content.
1174. Prohibit private note bodies.
1175. Add logging tests using planted secret markers.
1176. Create SupportBundleBuilder.
1177. Include application and OS version.
1178. Include sanitized non-secret settings.
1179. Include sanitized application logs.
1180. Include module status summaries.
1181. Include crash information only with explicit selection.
1182. Exclude source files and external database contents.
1183. Exclude notes.
1184. Preview bundle categories before generation.
1185. Write the bundle to a user-selected location.
1186. Add bundle-content tests.

## Phase 22 — Crash recovery and shutdown

1187. Create a startup crash marker.
1188. Remove the marker on clean shutdown.
1189. Detect previous unclean shutdown.
1190. Display a non-alarming recovery notice.
1191. Restore safe window geometry.
1192. Restore the selected workspace only after authentication.
1193. Restore the selected page conservatively.
1194. Offer note-journal recovery.
1195. Do not rerun Git mutations.
1196. Do not rerun SQL mutations.
1197. Do not restart services automatically after a crash.
1198. Observe possible surviving service processes.
1199. Show them as external/unmanaged until user action.
1200. Begin orderly shutdown by rejecting new work.
1201. Cancel cancellable background reads.
1202. Ask the running-services exit question.
1203. Flush note saves.
1204. Flush settings.
1205. Close database sessions in their owning threads.
1206. Stop file watchers.
1207. Flush and close DevHub logs.
1208. Destroy the authentication session.
1209. Add clean-shutdown integration tests.
1210. Add unclean-shutdown recovery tests.
1211. Add shutdown-with-running-services UI tests.
1212. Consider configurable local minidumps after v0.3.
1213. Never upload dumps automatically.

## Phase 23 — Accessibility, performance, and hardening

1214. Audit every form label association.
1215. Audit tab order.
1216. Audit visible focus.
1217. Audit icon-only accessible names.
1218. Audit table headers for screen readers.
1219. Add non-color status cues.
1220. Test primary flows using keyboard only.
1221. Test with Windows high-contrast settings where feasible.
1222. Verify text/background contrast.
1223. Verify reduced-motion behavior.
1224. Test layout at supported DPI scales.
1225. Test long usernames, paths, service names, and translations.
1226. Profile startup to authentication.
1227. Profile sign-in to usable shell.
1228. Profile idle memory.
1229. Profile workspace switching.
1230. Profile large workspace scans.
1231. Profile normal and large Git repositories.
1232. Profile log ingestion.
1233. Profile database pagination.
1234. Add performance telemetry only as local development instrumentation.
1235. Remove or disable development instrumentation in release UI.
1236. Define cache ownership and size limits.
1237. Add cache invalidation tests.
1238. Add multi-hour service/log/port soak tests.
1239. Add rapid workspace-switch stress tests.
1240. Add repeated lock/unlock stress tests.
1241. Add database open/close leak tests.
1242. Run static analysis.
1243. Run dependency vulnerability review before release.
1244. Perform a manual threat-model review.
1245. Review every log and history field for secrets.
1246. Review every destructive action and confirmation.

## Phase 24 — Packaging and release

Implementation status: Windows x64 packaging, deployment staging, a portable ZIP, installer source, dependency/license metadata, and release documentation are in place. The Release build links libsodium statically with the Qt-matched MinGW toolchain. The portable package includes the Qt runtime and verifies startup plus QSQLITE loading with the packaged files. The installer could not be compiled here because Inno Setup 6 is not installed. Qt licensing review, clean Windows 11 VM, install/upgrade/uninstall, release integration/soak checks, and the release tag remain release gates; do not publish until they pass.

1247. Add CMake install rules.
1248. Generate a Qt deployment script or run windeployqt in packaging.
1249. Include required Qt platform plugins.
1250. Include QSQLITE.
1251. Include libsodium runtime files as required.
1252. Verify no development DLLs are accidentally packaged.
1253. Add Windows application icon resources.
1254. Add version metadata.
1255. Add an asInvoker manifest.
1256. Create the Inno Setup script.
1257. Default to a per-user install location.
1258. Avoid requiring administrator privileges.
1259. Create Start menu shortcuts.
1260. Make desktop shortcut optional.
1261. Preserve Local AppData during normal upgrade.
1262. Make user-data removal an explicit uninstall option.
1263. Detect a running DevHub instance during upgrade.
1264. Add future code-signing hooks.
1265. Do not block unsigned private builds.
1266. Generate third-party license notices.
1267. Generate a dependency inventory.
1268. Generate SHA-256 checksums.
1269. Test installation on a clean Windows 11 VM.
1270. Test first-launch Create Account after installation.
1271. Test upgrade from every publicly released schema.
1272. Test uninstall with data preservation.
1273. Test explicit uninstall data removal.
1274. Test manual reinstall over an existing profile.
1275. Publish known limitations.
1276. Publish security-boundary documentation.
1277. Publish backup and recovery guidance.
1278. Run release-mode smoke tests.
1279. Run release-mode integration tests.
1280. Run the long soak suite.
1281. Create the release tag only after all release gates pass.

# Recommended First 30 Tasks

The first implementation batch should be exactly:

1. Confirm this document as the source of truth.
2. Add the decision-record template.
3. Record Qt Widgets.
4. Record Git CLI.
5. Record QSQLITE-first.
6. Record the local-account boundary.
7. Record Windows 11 x64 scope.
8. Review dependency licenses.
9. Pin the Qt 6.8 patch release.
10. Pin the MSVC toolset.
11. Create root CMakeLists.txt.
12. Set the minimum CMake version.
13. Configure CXX.
14. Require C++20.
15. Add CMakePresets.json.
16. Add Ninja Multi-Config.
17. Add build presets.
18. Add the CI preset.
19. Add the test option.
20. Add the packaging option.
21. Add the sanitizer option.
22. Create the repository directories.
23. Create the executable target.
24. Create project-options.
25. Create project-warnings.
26. Enable Qt code generation.
27. Locate Qt targets.
28. Link target-specific Qt dependencies.
29. Configure MSVC warnings.
30. Enable warnings as errors in CI.

# Suggested Git Milestones

| Milestone | Deliverable |
|---|---|
| v0.0.1 | Reproducible CMake project, CI, formatting, and tests |
| v0.0.2 | Shared errors, tasks, logging, application bootstrap |
| v0.0.3 | Dark theme tokens and approved application shell |
| v0.0.4 | Internal QSQLITE database and migrations |
| v0.0.5 | Create Account, Sign In, Lock, Unlock, and Sign Out |
| v0.0.6 | Workspace persistence, sidebar, and navigation |
| v0.0.7 | Bounded workspace detection |
| v0.0.8 | Read-only Git status, history, branches, and diff |
| v0.0.9 | Basic service lifecycle and captured output |
| v0.0.10 | Windows Port Monitor |
| v0.0.11 | Bounded file and service Log Viewer |
| v0.0.12 | Read-only SQLite browser and SQL query editor |
| v0.0.13 | Notes and lightweight file browser |
| v0.0.14 | Overview, settings, notifications, command palette |
| v0.1.0 | Integrated MVP |
| v0.2.0 | Git mutations and mature service workflows |
| v0.3.0 | Recovery, diagnostics, richer database and notes features |
| v0.5.0 | Database editing and Windows native process-tree launcher |
| v0.8.0 | Accessibility, security, performance, and soak hardening |
| v0.9.0 | Installer and upgrade release candidate |
| v1.0.0 | Production-ready Windows 11 x64 release |

# Suggested Repository Structure

    DevHub/
    ├── CMakeLists.txt
    ├── CMakePresets.json
    ├── vcpkg.json
    ├── README.md
    ├── LICENSE
    ├── SECURITY.md
    ├── CONTRIBUTING.md
    ├── cmake/
    │   ├── CompilerWarnings.cmake
    │   ├── StaticAnalysis.cmake
    │   ├── Sanitizers.cmake
    │   ├── VersionInfo.cmake
    │   └── Packaging.cmake
    ├── docs/
    │   ├── architecture/
    │   │   ├── overview.md
    │   │   ├── authentication.md
    │   │   ├── persistence.md
    │   │   ├── concurrency.md
    │   │   ├── security.md
    │   │   └── ui-design.md
    │   ├── decisions/
    │   └── product/
    ├── resources/
    │   ├── devhub.qrc
    │   ├── icons/
    │   ├── branding/
    │   └── windows/
    ├── src/
    │   ├── CMakeLists.txt
    │   ├── app/
    │   │   ├── main.cpp
    │   │   ├── ApplicationBootstrapper.h
    │   │   ├── ApplicationBootstrapper.cpp
    │   │   ├── ApplicationPaths.h
    │   │   └── ApplicationPaths.cpp
    │   ├── domain/
    │   │   ├── common/
    │   │   ├── errors/
    │   │   └── identifiers/
    │   ├── application/
    │   │   ├── commands/
    │   │   ├── events/
    │   │   ├── tasks/
    │   │   ├── navigation/
    │   │   └── notifications/
    │   ├── infrastructure/
    │   │   ├── database/
    │   │   │   ├── migrations/
    │   │   │   └── repositories/
    │   │   ├── filesystem/
    │   │   ├── logging/
    │   │   └── settings/
    │   ├── platform/
    │   │   ├── interfaces/
    │   │   └── windows/
    │   │       ├── process/
    │   │       ├── ports/
    │   │       ├── terminal/
    │   │       └── trash/
    │   ├── presentation/
    │   │   ├── shell/
    │   │   ├── authentication/
    │   │   ├── theme/
    │   │   ├── widgets/
    │   │   ├── dialogs/
    │   │   ├── delegates/
    │   │   └── formatting/
    │   ├── modules/
    │   │   ├── account/
    │   │   │   ├── domain/
    │   │   │   ├── application/
    │   │   │   ├── infrastructure/
    │   │   │   └── presentation/
    │   │   ├── workspace/
    │   │   ├── detection/
    │   │   ├── git/
    │   │   ├── services/
    │   │   ├── ports/
    │   │   ├── logs/
    │   │   ├── database/
    │   │   ├── notes/
    │   │   ├── files/
    │   │   ├── overview/
    │   │   ├── search/
    │   │   ├── settings/
    │   │   └── notifications/
    │   └── shared/
    │       ├── async/
    │       ├── models/
    │       ├── text/
    │       ├── validation/
    │       └── testing/
    ├── tests/
    │   ├── CMakeLists.txt
    │   ├── unit/
    │   ├── integration/
    │   ├── ui/
    │   ├── fixtures/
    │   └── helpers/
    │       ├── output_process/
    │       ├── child_process_tree/
    │       ├── stubborn_process/
    │       └── port_listener/
    ├── packaging/
    │   ├── inno/
    │   ├── licenses/
    │   └── signing/
    └── tools/
        ├── diagnostics/
        ├── formatting/
        └── release/

# Most Difficult Parts

## Account security without overstating protection

The UI must look like a local account system while remaining honest: it prevents access through DevHub but does not encrypt external files. Password hashing, session state, Lock, Sign Out, inactivity behavior, forgotten-password reset, and eventual secret encryption are related but distinct systems. Conflating them would create misleading security claims.

## Windows process-tree ownership

QProcess can launch and observe a direct child, but development commands often spawn grandchildren. Reliable ownership requires careful CreateProcessW, pipe, suspended-start, and Job Object work. Incorrect handling may orphan services, kill unrelated processes after PID reuse, or deadlock output pipes.

## Git repository states

The normal clean/dirty case is simple. Worktrees, detached HEAD, unborn branches, hooks, signing, LFS, unusual filenames, conflicts, credentials, and partial mutation failures make a serious client difficult. Only documented machine-readable formats should be parsed.

## High-throughput logs

The hard part is not QFile reading. It is handling partial encoding, rotation, truncation, a producer faster than the UI, filtering, pause/resume, and strict memory bounds at the same time.

## QSQLITE query cancellation

Qt SQL is the chosen first backend, but cancellation and low-level SQLite control may be limited. The abstraction must make a direct backend possible without prematurely building two database stacks.

## Safe database editing

Editing rows is unsafe when there is no stable row identity or application triggers/invariants are unknown. Read/query functionality should mature before visual editing is enabled.

## Cross-module state

Service, port, log, Git, and Overview states interact. Direct widget connections create coupling; an unrestricted event bus creates invisible behavior. Typed stores, commands, and a limited factual event hub require consistent discipline.

## Visual fidelity with native Windows behavior

The approved screenshots use custom dark surfaces and macOS-like chrome. Reproducing the character while retaining Windows snapping, focus, accessibility, DPI behavior, and performance requires separating visual references from literal platform imitation.

# Decisions to Revisit Deliberately

These are not unresolved blockers, but changing them later is expensive:

1. The exact Qt 6.8 patch release.
2. The exact Argon2id production parameter profile.
3. Username case-sensitivity and normalization details.
4. The default state of Keep me signed in for this session.
5. The default inactivity timeout when Keep session is not selected.
6. Whether the authentication window and main window are one native window or two.
7. Whether a custom Windows title bar is worth the complexity after MVP.
8. The final logo and icon license/ownership.
9. The exact Local AppData versus AppData split.
10. The internal database WAL and backup policy.
11. The threshold proving QSQLITE must be replaced for database browsing.
12. The default service graceful-stop strategy on Windows.
13. The exact meaning of Leave Services Running after QProcess ownership ends.
14. The default Git pull prompt behavior when repository configuration is absent.
15. Which Git mutations ship in v0.1 versus v0.2.
16. The maximum scan wall time in addition to depth and file count.
17. The log byte cap paired with 100,000 lines.
18. The strategy for unsupported log encodings.
19. The maximum preview size for files and BLOBs.
20. Whether local minidumps are enabled by default.

# Final Scope-Control Rule

If schedule pressure appears, preserve:

- Local account and reliable session behavior.
- Workspace Manager.
- Overview.
- Git status, history, diff, and basic mutations.
- Run and Services.
- Port Monitor.
- Log Viewer.
- SQLite read/query workflows.
- Local Notes.
- Settings.

Postpone first:

- Advanced Git operations.
- Visual SQLite row editing.
- Schema designer.
- In-project notes.
- Global file-content search.
- Complex animation.
- Native Windows notifications.
- Auto-updater.
- Public plugin SDK.
- Windows ARM.
- Linux implementation.
- Advanced automatic detection.

The finished v1 should be smaller, predictable, secure in its claims, and testable rather than broad but unreliable.
