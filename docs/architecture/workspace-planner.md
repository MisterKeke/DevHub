# Workspace Planner architecture

**Status:** Phases 1 and 2 implemented · **Reviewed:** 2026-10-22
**Scope:** Local Planner storage, services, and embedded Overview UI. SomethingQt import is Phase 3.

## 1. Feature scope and non-goals

Workspace Planner is a local, workspace-owned task manager stored in DevHub's internal SQLite
database. The embedded Overview panel provides a summary, Kanban board, list and calendar views,
task editing, and development metadata. SomethingQt import is planned for Phase 3; no source import
or synchronization is part of the current implementation.

The Planner belongs directly on the existing Overview, after the “Workspace Overview” heading and
before the existing Git, Services, Ports, Logs, Database and Notes cards. It can collapse and
expand. It must not add a sidebar item, a new module, or a page in `CurrentPageStack`; it must not
replace the existing Overview cards.

The import is a one-time copy. A successful import creates DevHub-owned records with new local
IDs, records source provenance for duplicate detection, and leaves the source database untouched.
Later edits and deletion are local; no automatic refresh, synchronization or write-back is in
scope. This plan does not include team sharing, remote task services or a new application
framework.

## 2. Existing architecture findings

### Overview and shell

`MainWindow` creates an `OverviewPage` (Qt Widgets) and the other Widgets module pages in a
`CurrentPageStack`, but that stack is parented to a separate “Workspace tools” dialog. The visible
main shell is `resources/qml/MainShell.qml`, loaded by `QQuickView` and hosted in the main window.
The QML Overview currently contains the heading, workspace state, and the six existing cards. It
gets card data from `MainWindow::overviewSections()`, which adapts an
`OverviewStateComposer` snapshot. Selecting a nonzero module opens the separate Widgets dialog;
selecting Overview returns to the QML shell. Thus the visible Overview is QML, not
`OverviewPage`.

The Planner is rendered by `resources/qml/PlannerPanel.qml`, inserted in the existing
`MainShell.qml` Overview column between the heading row and the current `GridLayout`. The six
existing cards remain below it. The panel is an Overview child, not a page or navigable module, and
uses the shell's vertical `Flickable` and semantic colors exposed by `themeController`.

The Widgets `OverviewPage` constructs its six card frames once, updates their values from
`setSnapshot()`, and rebuilds only the responsive grid when its width crosses the 700- and
1120-logical-pixel breakpoints. Dynamic card detail/action children are removed with
`deleteLater()` and recreated on updates; normal QObject parent ownership destroys the remaining
page tree. The active QML Overview instead uses a `Repeater` and responsive `GridLayout`, refreshed
when `MainWindow` emits `shellChanged`. Preserve both six-card sets in their respective views;
the Planner's primary lifecycle is the QML panel/model refresh, not rebuilding the card widgets.

Do not embed a QWidget with `QQuickWidget` or add the Planner to the old Widgets page stack: that
would introduce a second UI implementation and would not put the Planner in the visible Overview.
Keep `OverviewPage` unchanged unless a future shell migration makes it an active representation.
If that happens, reconcile the two Overview implementations in that migration rather than
building two Planner UIs now.

`OverviewStateComposer` is a debounced, backend-free combiner for the existing summary cards. The
Planner's task board and controls should have their own controller/model and should not be encoded
as a seventh card or folded into that snapshot. `MainWindow` already exposes QML properties and
slots through the `shellController` context property, so a small `WorkspacePlannerController` can
be exposed beside that property without changing navigation.

### Services, lifecycle and workspace context

`ApplicationBootstrapper` constructs account, workspace, service, notes, notification and database
services and injects them into `MainWindow`. `MainWindow` owns the visible UI and maintains the
overview snapshot as workspace-scoped services publish updates. `WorkspaceContext` carries the
active domain workspace and a cancellation token; switching or clearing it cancels jobs from the
previous context. `TaskScheduler` runs bounded background jobs in a `QThreadPool`, propagates a
workspace ID, generation, correlation ID and cancellation token, and delivers detached results
back to its owning thread. It can cancel and block work for an archiving workspace.

Workspace-owned rows conventionally reference `workspaces(id)` with `ON DELETE CASCADE`. A
workspace archive only sets `archived_at_utc`; it retains the registration and its data so restore
can reuse the same workspace ID. Workspace listings and repository operations are scoped to the
signed-in account. Planner queries must additionally require the current workspace ID, and must
verify that this workspace belongs to the active account.

The local account is an access boundary, not encryption. Session locking, sign-out and account
changes already gate the UI and clear Assistant access; the Planner must also cancel its jobs,
close task editors, clear task data from in-memory models and reject late results when session
scope changes. Archiving must cancel/block Planner work before archiving, while retaining task
rows for a later restore.

### SQLite and build conventions

The internal database is `LocalAppData/DevHub/data/devhub.db`. `InternalDatabaseManager` owns a
uniquely named Qt SQL `QSQLITE` connection on its QObject thread, enables foreign keys, sets a
5-second busy timeout, requests and verifies WAL, integrity-checks before migration, and applies
embedded migrations transactionally. `SqlTransactionGuard` rolls back uncommitted work. Released
migrations are immutable; migration files are embedded by `resources/database_migrations.qrc`.
The migration sequence now ends at `0009_workspace_planner.sql`. The new migration is additive;
released migrations 0001–0008 remain unchanged.

The production application already uses Qt Widgets, Qt Quick/QML, Qt SQL and Qt Test. CMake lists
sources explicitly and declares QML files in its QML module. No additional framework or database
engine is needed.

## 3. Suggested component structure

Use the current domain/application/infrastructure/presentation boundaries and keep the first
vertical slice small.

| Layer | Suggested types | Responsibility |
|---|---|---|
| Domain | `PlannerTask`, `PlannerSubtask`, `PlannerStatus`, `TaskOrigin`, `SomethingQtProvenance`, `PlannerTaskValidator` | Local task value model, status and input rules, provenance values. No Qt SQL or QWidget dependencies. |
| Application | `PlannerTaskFilter`, `IPlannerTaskRepository`, `PlannerService`, `SomethingQtImportService` | Workspace-scoped CRUD and import use cases, validation, duplicate policy, cancellation and typed `Result<T>` failures. |
| Infrastructure | `SqlPlannerTaskRepository`, `SomethingQtTaskImporter`, read-only source connection guard | Bound Qt SQL statements, transactions and migrations; direct, bounded-memory reads of the source schema. |
| Presentation | `WorkspacePlannerController`, `PlannerTaskListModel` (`QAbstractListModel`), `WorkspacePlannerPanel.qml`, task editor QML dialog | Expose detached snapshots and async commands to QML; keep all model/widget mutation on the GUI thread. |

The controller can be owned by `MainWindow` or by the existing application bootstrap object, then
exposed to `MainShell.qml` through the `QQuickView` root context. Keep UI state and QML-specific
roles out of the domain model. Use a single local task shape for both task origins; an optional
provenance value distinguishes imported tasks without creating a second imported-task type.

The implemented fixed status set is `backlog`, `todo`, `in_progress`, `in_review`, and `done`.
New tasks default to incomplete `todo`; completion and `done` are synchronized in
`PlannerRules`. `difficulty` remains task data, not a Kanban status.

## 4. Database design

Migration `0009_workspace_planner.sql` adds these workspace-owned tables:

- `planner_tasks` stores DevHub task UUID, title, full description, completion/status, board
  position, UTC creation/update timestamps, date-only start/due dates, low/medium/high priority and
  optional easy/medium/hard difficulty,
  local revision, estimate and actual tracked minutes, optional milestone, Git branch and GitHub
  issue/PR URLs, notes, blocked reason and source type. A check keeps `done` and completion equal.
- `planner_tags` and `planner_task_tags` normalize tags per workspace. Composite foreign keys keep
  a task and its tags in the same workspace.
- `planner_subtasks` stores local UUID, source subtask ID text, title, completion and position.
- `planner_milestones` stores date-only planning dates, completion and its own optimistic revision.
- `planner_task_dependencies` uses composite task/workspace foreign keys and disallows self-links.
- `planner_import_records` separates source profile/task/revision and original timestamps from
  editable task state. Source IDs and revisions are text to retain full 64-bit values; uniqueness
  on workspace/profile/source ID supports future duplicate detection. Original source timestamp
  strings are kept verbatim; local timestamps use normalized UTC ISO values.

All workspace-owned rows cascade only when the workspace row is permanently deleted. Archiving
continues to set `archived_at_utc`, preserving task data for restore. Repository queries and
mutations additionally scope through the signed-in account. DevHub task UUIDs come from the
existing `IIdGenerator`; SomethingQt IDs are never primary keys. User text is not truncated.
`due_date` and `start_date` use `YYYY-MM-DD`. Actual tracked minutes are stored as an editable
total; detailed `planner_time_entries` are not part of this phase.

Dependency insertion checks the graph and inserts inside one SQL transaction. Removing a
milestone clears task references and removes the milestone in one transaction. Local task updates
use `WHERE revision=?` and increment the revision atomically; source revision is provenance only.

## 5. SomethingQt import design

### Verified source contract

The public `MisterKeke/Something-Qt` repository was reviewed on `main` at commit
[`dce73d3826d4641434cecfd52576f396262fe701`](https://github.com/MisterKeke/Something-Qt/commit/dce73d3826d4641434cecfd52576f396262fe701).
The source model is `src/domain/task.h`; validation is in `src/domain/task_rules.cpp`; use-case
commands are in `src/services/task_service.cpp`; persistence and revision checks are in
`src/storage/repositories.cpp`; schema migrations are in `src/storage/schema_migrator.cpp`.

The current `Task` model contains numeric `id`, `title`, `description`, `completed`, textual
`createdAt`, `updatedAt`, and `dueDate`, `priority`, optional/empty `difficulty`, integer
`revision`, tag names, and ordered subtasks (`id`, `title`, `completed`, `position`). Storage v1
has `tasks(id,title,description,is_completed,created_at,updated_at,due_date,priority,difficulty,
revision)`, `tags(id,name,name_normalized)`, `task_tags(task_id,tag_id)`, and
`task_subtasks(id,task_id,title,is_completed,position,created_at)`. The subtask domain object does
not expose `created_at`; tag names alone do not expose source tag IDs or normalized values. Preserve
these extra storage values in the source snapshot as well as all modeled task fields.

Source validation trims title/description/date, accepts priorities `low|medium|high`, difficulties
`easy|medium|hard`, and a valid ISO calendar date. It case-folds tags, deduplicates them, allows at
most 32 tags of at most 64 Unicode code points, and allows at most 100 subtasks only when difficulty
is `hard`. Task edits use an expected revision; update, completion and subtask-completion changes
increment it and return a conflict if it no longer matches. Revision is a concurrency counter,
not a historical change log. The source schema's task `id` and revision are SQLite integers; the
source must be read without passing them through a JSON double.

The source has only `is_completed`; it has no completion timestamp or richer status value. Do not
invent a historical `completed_at_utc` for an imported completed task. Leave that local field null
until a DevHub action records a completion time.

### Import flow and ownership

Use an explicit `SomethingQtTaskImporter`, separate from Assistant presentation. Ask the user to
select/confirm a SomethingQt profile file from the Planner. Open it through a uniquely named
`QSQLITE` connection with read-only open options, on the worker thread that will read it. Validate
the required table/column affinities and recognized migration history before reading. Read a
consistent snapshot in ID order, including tags and subtasks; do not use display truncation,
truncate input fields, migrate, checkpoint or write to the source. Normal WAL reads are required;
missing live WAL/SHM access and a busy source should produce an actionable error.

For each source task, create a new DevHub task ID and copy the task into the active workspace's
local task model. Map `completed=true` to `status='done'`, otherwise to `todo`; copy title,
description, due date, priority, difficulty, tags and subtask content/order. Allocate new local
subtask and tag IDs. Retain the original source task ID, subtask IDs, tag IDs/normalization,
timestamps, revision and exact imported values in the immutable provenance snapshot. Record
`imported_at_utc`; DevHub's `created_at_utc`, `updated_at_utc` and `revision` describe the local
record lifecycle. This keeps source timestamps available without making future local changes look
like changes in SomethingQt.

Use a unique source identity `(workspace_id, source_type, source_profile_identity, source_task_id)`
in `planner_import_records`.
On conflict, leave the existing local task unchanged and report it as already imported. Never
update a duplicate from source data. A source task deleted after import remains in DevHub. A rerun
may import newly added source tasks while skipping those already copied.

The source schema has no stable profile UUID in the verified task contract. A practical initial
`source_profile_identity` is a hash of the normalized canonical file path; it avoids storing the path
with every task but means moving the database can change its identity and allow a duplicate copy.
Surface that limitation in the preview and allow the user to choose the existing source identity
when the same profile moved. Do not silently merge by title or task content. Phase 2 should add an
immutable source snapshot for source-only values such as original tag IDs and normalized names.

### Why the Assistant reader is not the importer

`SomethingTasksReader` is a read-only, short-lived Assistant adapter, but it deliberately shapes
data for bounded tool output: searches return at most 20 tasks, offsets stop at 100,000, task
descriptions at 8,192 characters, titles at 512, at most 50 subtasks, and at most 16 displayed
tags. Serialized search and detail responses are capped at 48 KiB and 40 KiB. It also returns
response-specific truncation flags and may omit rows or displayed tags to fit those limits. Its
multi-page searches are separate snapshots, so source edits can shift offset pages. Using these
responses as import records would silently omit or alter data and cannot provide a complete
consistent profile snapshot.

Keep the existing reader's Assistant scope/disclosure behavior unchanged. The importer may reuse
its read-only open and schema-validation knowledge, but should have a separate import result and
row reader with no response-size or field-display limits. Do not treat Assistant disclosure as
the authorization for import; the user explicitly starts import from the Planner.

## 6. UI integration strategy

`PlannerPanel` is placed after the Overview heading and before the existing card `GridLayout`. It
provides collapse/expand, summary, board/list/calendar views, filters, task editing and an explicit
status selector.

`PlannerController` and `PlannerTaskModel` are exposed beside `shellController`; the controller
binds to the active workspace and uses `PlannerService` for CRUD. List roles back virtualized QML
views. Status and ordering changes persist through service calls. Board drag-and-drop is deferred;
accessible up/down controls change order explicitly.

The import button remains disabled with a Phase 3 explanation. QML does not open SQL connections;
task content is plain text and URLs are validated by the service. Theme colors come from
`themeController`.

## 7. Workspace and account isolation

Every application request, query and controller state carries an explicit active workspace ID.
Repository methods should require both the workspace ID and active account ID, and reject archived
or foreign-account workspaces for writes. The primary key is local and global, while all list,
summary, board, calendar, tag and import-dedup queries are workspace filtered. Composite
workspace foreign keys enforce same-workspace links.

On workspace change, clear the previous task model before loading the new workspace. Current
Planner service calls are synchronous on the database owner's thread, so no stale read completion
can publish after a switch. Archive retains task rows with the workspace registration. On
lock/sign-out, clear the in-memory task model; the locked-session page hides Planner content.

## 8. Threading and concurrency strategy

Never pass a `QSqlDatabase`, `QSqlQuery` or repository holding a live Qt SQL connection between
threads. `InternalDatabaseManager::database()` is valid only on its owning thread, and the existing
Overview and service repositories use that main-thread connection. Moving it to `TaskScheduler`
workers would violate Qt SQL affinity.

The `SqlPlannerRepository` checks that each operation runs on `InternalDatabaseManager`'s owning
thread. `PlannerController` currently uses synchronous service calls on that thread. Workspace
switches clear and reload the model in order, so there are no queued Planner reads to invalidate.
If workloads make Planner operations slow, move them to `TaskScheduler` with a separate, uniquely
named database connection opened in each worker thread and return detached task values.

For a large import, preview the source count and stream rows into bounded batches rather than
loading all tasks into memory. Commit completed batches; check cancellation between source rows and
before each batch commit. If cancelled, retain prior committed tasks and report partial counts; a
rerun safely resumes by provenance uniqueness. This makes cancellation responsive without a long
write lock. Use one stable read transaction on the read-only source for the run; close both
connections on their creating worker before it exits. Emit progress through the existing scheduler
progress mechanism. Drop a result if its workspace, session scope or generation is obsolete.

Use `revision` in update predicates (`WHERE id=? AND workspace_id=? AND revision=?`) and increment
it atomically. The repository's unique source index remains the final guard against repeated
imports even with another connection or process.

## 9. Error handling

Follow existing `Result<T>` and `AppError` patterns. Domain validation errors are distinct from
not-found, conflict, database-unavailable, source-missing/unreadable, incompatible-schema,
source-busy, cancellation and shutdown/session-scope errors. Map Qt SQL errors through the existing
database mapper and include the operation/correlation ID in technical diagnostics. Show actionable
messages inline in the Planner or through the existing notification system; do not expose SQL text,
database paths, task descriptions or source contents in logs/support bundles.

An import result should report per-run counts and whether the run completed, was cancelled, or
failed. Source read-only connection and schema failures must leave local data unchanged for the
uncommitted batch. Duplicate source identity is a normal result, not an error. On revision
conflict, reload the task and ask the user to review current values rather than overwriting them.

## 10. Security and privacy

Imported tasks, tags and descriptions are user data. They remain in DevHub's local account
database and workspace scope; the local account does not encrypt them. Do not include task content
in diagnostics, telemetry, support bundles or Assistant prompts by default. Keep import provenance
minimal and avoid persisting raw source paths in each task. Use prepared SQL statements and bind
values. Treat title/description/tags as plain untrusted text, never HTML or QML source. Do not
invoke a shell with a branch name or URL; validate branch references and allow only appropriate
GitHub `https` URLs for explicit open-link actions.

The source connection is read-only, performs no source migrations and owns no source lock or
checkpoint action. It must be closed on cancellation and every error path. No connection string,
source path or task data should be passed to an external service.

## 11. Test strategy

Use the existing Qt Test and CTest conventions, including temporary SQLite databases. Phase 1
coverage in `devhub_planner_tests` and `devhub_database_tests` includes:

- fresh migration to schema 9 and foreign-key integrity;
- workspace/account isolation and archive/restore retention;
- CRUD, Unicode and a large description, tag replacement, subtask ordering, search and pagination;
- completion/status transitions, board ordering, revision conflicts and stale workspace reads;
- milestones, dependency cycles, self/cross-workspace dependency rejection;
- failed tag-write rollback, 64-bit source IDs/revisions and persistence after reopening SQLite.

Still needed in later phases:

- importer read-only behavior, schema/version rejection, long text and 64-bit source IDs,
  duplicate provenance, full tag/subtask field preservation, source WAL/busy handling, cancellation,
  partial-batch rerun and source task deletion after import;
- stale results after workspace switch, archive, account lock and sign-out;
- QML placement/collapse/expand, all three views, empty/error/loading states, keyboard access and
  narrow Overview widths; assert existing six cards remain present below the panel.

## 12. Identified risks

1. **Two Overview implementations:** QML is active and `OverviewPage` is retained in the Widgets
   dialog stack. Keep the Planner in visible QML and test the main shell, or later shell work could
   expose a divergent Overview.
2. **Source profile identity:** the verified SomethingQt task schema has no stable profile UUID.
   Path-derived identity changes on relocation; provide an explicit duplicate-source resolution.
3. **Source schema drift:** task tables and validation may change. Validate the schema version and
   required affinities; reject incompatible inputs instead of guessing or truncating.
4. **Qt SQL affinity and concurrent app writes:** planner workers need independent connections to
   the same WAL database. Keep operations serialized within Planner, writes short, and handle
   `SQLITE_BUSY` and session cancellation.
5. **Import size and partial progress:** full profile import may take time. Preview counts,
   stream in batches, show progress, and make rerun idempotent after cancellation.
6. **Status mapping:** SomethingQt only has a completion boolean. Map it deterministically to
   `todo`/`done`; other Planner states have no source equivalent.
7. **Data privacy:** local account locking does not encrypt SQLite. Keep content out of diagnostics
   and clear all cached state on lock and account change.
8. **QML vertical space:** the current shell is a scrollable Overview with a responsive card grid.
   The Planner must contribute accurate implicit height, scroll as one document and preserve access
   to the cards below it.

## 13. Implementation phases

1. **Phase 1 — local storage and services (implemented):** migration 0009, typed task/subtask/
   milestone values, validation and status rules, account/workspace-scoped SQLite repository,
   application service, optimistic task/milestone revisions, relations, filtering/search/sorting/
   pagination, and SQLite-backed tests. No UI or importer was added.
2. **Phase 2 — Overview UI (implemented):** add the controller/model and collapsible QML panel to
   the visible Overview, with board/list/calendar, task editing, filters and persistent ordering.
   Drag-and-drop remains deferred in favor of explicit persisted status/order controls.
3. **Phase 3 — SomethingQt import:** add the independent read-only import flow with schema
   validation, new local IDs, source provenance, duplicate detection, preview, progress,
   cancellation and resumable batches. Do not modify SomethingQt or synchronize writes back to it.
4. **Later hardening:** add detailed time entries if needed, and cover accessibility,
   account/session changes, migration recovery, larger imports and responsive Overview layouts.

## 14. Overall product acceptance criteria

- Planner content is visible in the existing main-window Overview after “Workspace Overview” and
  before all six original cards; it collapses and expands and adds no navigation entry or module.
- New and imported tasks share one local task model, use new DevHub IDs, survive source shutdown or
  source task deletion, and can be edited/deleted without touching SomethingQt.
- Source import preserves every task field and source-only tag/subtask storage value without
  truncation, records provenance, skips duplicates without overwriting local edits, and is
  repeatable after cancellation.
- Every task and relation is scoped to one workspace and the active account; switching, archiving,
  locking and signing out do not leak stale task content or allow stale UI updates.
- CRUD calls stay on the database owner's thread, and validation/conflict failures have user-visible
  handling. Import remains a Phase 3 concern.
- Summary, Kanban, list and calendar are projections of the same persisted task records; existing
  Overview cards remain unchanged below the Planner.
- Migrations are additive, transactional and rollback-safe, and documented implementation tests
  cover isolation, ownership, compatibility, cancellation and UI integration.

## Phase 2 completion and Phase 3 handoff

The current app uses a Qt Quick main shell, Widgets feature pages in a secondary dialog, an
account-scoped internal QSQLITE database with immutable embedded migrations, a cooperative
`TaskScheduler`, and workspace-aware cancellation/archive lifecycle. Workspace Planner now appears
in the visible Overview as a QML child panel, with C++ application services and models exposed
through the existing shell; it does not enter `CurrentPageStack` or replace the six cards.

Phase 1 supplies local models and validation, migration 0009, account/workspace-scoped CRUD,
relations, filtering, sorting, pagination, completion rules, optimistic revisions and SQLite tests.
Phase 2 adds the embedded Planner panel, virtualized models, workspace-scoped view/collapse
settings, and controller tests. Service calls currently run synchronously on the database owner's
thread. Actual tracked time is a task total; detailed time entries remain unimplemented. GitHub
links accept HTTPS github.com URLs, while Git branch references are stored as metadata. Phase 3
import remains unimplemented.
