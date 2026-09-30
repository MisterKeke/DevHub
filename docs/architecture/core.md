# Shared core and startup

The C++ namespace policy follows the source tree: `devhub::domain` holds domain values, `devhub::application` holds use cases and coordination, `devhub::infrastructure` holds OS and persistence adapters, `devhub::presentation` holds UI-facing presentation helpers, and `devhub::shared` holds cross-cutting primitives. Namespaces use lower case.

## Results and errors

Use `devhub::shared::common::Result<T>` for success or failure. Errors carry a stable `ErrorCode`, `ErrorCategory`, severity, user-safe text, technical text, optional native/workspace/correlation data, operation name, and retryability. Background tasks return `TaskOutcome<T>`, which represents cancellation as its own alternative to success or failure.

Use `ErrorPresenter` to produce user text and expandable details. `TechnicalDetailsFormatter` and `ErrorPresenter` pass technical and user strings through `RedactionService` before displaying them.

## Application paths

`ApplicationPaths::resolveAndPrepare()` uses `QStandardPaths` to resolve local application data, config, and cache locations. It creates the log, backup, and user subdirectories and checks each required location by opening a temporary write probe. A path failure returns a structured `AppError`; it never silently selects the current working directory.

The internal QSQLITE database is stored at `ApplicationPaths::internalDatabaseFile` in the Local AppData `data` directory. `InternalDatabaseManager` gives each connection an explicit UUID-backed name and creates, uses, and removes it only from its owning thread. It enables foreign keys, a five-second busy timeout, and WAL mode, then checks the database before applying versioned SQL resources. A migration failure or newer schema enters recovery mode; the database is never deleted or recreated automatically. Recovery mode can make a timestamped copy in the local backups directory before the user retries or quits.

## Background tasks

`TaskScheduler` runs short work on a bounded `QThreadPool`. Each request carries a task ID, correlation ID, optional workspace ID, module key, and generation. Configure per-module limits with `setModuleConcurrencyLimit()`. Submitting a newer generation for a workspace/module pair makes older completions stale; stale results are discarded. Work receives a cooperative `CancellationToken`, and can report progress through its `TaskContext`.

## Notifications and events

`ApplicationEventHub` exposes typed Qt signals. Add a typed signal when a stable application fact needs to reach another module; do not add string event names or map-shaped payloads. `NotificationService` emits typed notifications without depending on widgets and suppresses repeated deduplication keys within its configured interval.

## Startup and shutdown

`main()` constructs Qt and injectable clock/ID providers, then delegates startup to `ApplicationBootstrapper`. Bootstrap sets application identity before resolving paths. Fatal path errors are redacted into a local temporary startup log and shown in `RecoveryModeWindow`, which supports retry and quit. Startup timing uses a monotonic elapsed timer.

After the database is ready, bootstrap inspects the single-account repository and shows the modal
Create Account or Sign In screen. It creates the authenticated session before opening `MainWindow`.
Lock swaps the main shell for an opaque unlock page in the same stack, preserving the selected
page while keeping it hidden. The application session is cleared on exit.

`ApplicationLifecycle` enforces valid lifecycle transitions. `ShutdownCoordinator` rejects new scheduler work, requests cooperative cancellation, and records orderly shutdown as Qt exits. Long-running work must check its token; widgets and models must only be changed on the GUI thread.
