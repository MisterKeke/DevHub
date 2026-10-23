# Workspace Planner architecture

**Status:** Phases 1–6 implemented · **Reviewed:** 2026-10-23
**Scope:** Local Planner storage, embedded Overview UI, independent SomethingQt task import, developer planning metadata, and saved-service integration.

## 1. Feature scope and non-goals

Workspace Planner is a local, workspace-owned task manager stored in DevHub's internal SQLite
database. The embedded Overview panel provides a summary, Kanban board, list and calendar views,
task editing, development metadata, and a read-only SomethingQt task copy flow. Imported tasks are
independent DevHub records; automatic refresh, synchronization and write-back are not implemented.

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

The internal database is `LocalAppData/DevHub/data/devhub.db`. The current schema version is 12;
the Planner migration history and current table contract are documented in
[`workspace-planner-database.md`](workspace-planner-database.md). `InternalDatabaseManager` owns a
uniquely named Qt SQL `QSQLITE` connection on its QObject thread, enables foreign keys, sets a
5-second busy timeout, requests and verifies WAL, integrity-checks before migration, and applies
embedded migrations transactionally. `SqlTransactionGuard` rolls back uncommitted work. Released
migrations are immutable; migration files are embedded by `resources/database_migrations.qrc`.
Planner migrations 0009–0012 are additive and transactional. Migration 0010 preserves existing
import provenance and adds a copy number for explicitly requested independent copies; migrations
0001–0008 remain unchanged.

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
  editable task state. Source IDs and revisions are text to retain full 64-bit values. Migration
  0010 adds `copy_number` and scopes uniqueness to workspace/source type/profile/source ID/copy
  number, enabling duplicate skips and explicitly requested independent copies. Original source
  timestamp strings are kept verbatim; local timestamps use normalized UTC ISO values.
- `planner_time_entries` records manual durations, while `planner_action_runs` stores bounded
  metadata for explicitly started saved-service actions. Migration 0012 adds `linked_service_id`
  and the per-task action history; service output remains in the existing bounded Logs buffers.

All workspace-owned rows cascade only when the workspace row is permanently deleted. Archiving
continues to set `archived_at_utc`, preserving task data for restore. Repository queries and
mutations additionally scope through the signed-in account. DevHub task UUIDs come from the
existing `IIdGenerator`; SomethingQt IDs are never primary keys. User text is not truncated.
`due_date` and `start_date` use `YYYY-MM-DD`. Actual tracked minutes are the aggregate of manual
time entries; users edit the history rather than changing the aggregate directly. Migration 0011
adds `planner_time_entries` and indexes for time history, branch, origin, milestone, due-date and
priority filters.

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
not expose `created_at`; tag names alone do not expose source tag IDs or normalized values. DevHub
copies all tag names and retains original subtask IDs, but does not retain SomethingQt tag IDs,
normalized tag strings or subtask creation timestamps.

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

`SomethingQtImportReader` is separate from Assistant presentation. The Planner reuses the
configured Assistant profile path and lets the user browse to any compatible SQLite file. Each
operation opens a uniquely named `QSQLITE` read-only connection on its scheduler worker, validates
required table/column affinities and contiguous migration history, then reads in a consistent
transaction. It does not change journal mode, migrate, checkpoint or write to the source. Normal
WAL reads are used; missing live WAL/SHM access and busy sources produce actionable errors.

For each source task, create a new DevHub UUID and copy its full title, description, completion,
date, priority, difficulty, tags and all subtasks with source positions and completion states.
Subtasks receive new local UUIDs and retain their original IDs in `source_subtask_id`. Import
provenance retains the source task ID, source revision, original timestamps, profile identity and
import time. The local revision starts at 1; local creation/update times describe the DevHub copy.
Completed source tasks become `done`; incomplete tasks become `todo`.

Migration 0010 keys provenance by
`(workspace_id, source_type, source_profile_identity, source_task_id, copy_number)`. Default
imports skip existing source tasks in that workspace. An explicit confirmation can create another
independent copy. Imports to a different workspace are independent. No import overwrites or merges
an existing task; deleting or editing the source later does not affect a DevHub copy.

SomethingQt exposes no stable profile UUID. DevHub identifies a source using a hash of the
normalized canonical path and file birth time; replacing the file is detected on the next operation.
Moving a profile changes its identity and may allow another import. Source tag IDs are not retained;
the complete tag names are copied into the workspace's case-folded tag model.

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

The Import from SomethingQt button opens the embedded importer dialog. It supports verification,
search and filters, paged multi-selection, tag-based/all import, a full plain-text preview,
destination workspace selection, explicit duplicate-copy confirmation, progress and cancellation.
QML does not open SQL connections. Source reading is asynchronous; each target task is committed
atomically on the database owner's thread after rechecking session and workspace authorization.

### Phase 4 developer planning controls

The task editor keeps Planner notes separate from the original task description and supports
existing local Git branch associations, estimates, milestone assignment, GitHub Issue/PR URLs,
subtasks and dependency edits. Branch names are read asynchronously through the existing
`GitService` and `TaskScheduler`. Opening a branch selects it in the existing Git module; it never
checks out or creates a branch. A deleted association remains visible as missing and can be removed.
GitHub links accept only HTTPS `github.com/<owner>/<repo>/issues/<number>` or `/pull/<number>` paths,
and open only after the user chooses the corresponding button.

Milestones support workspace-scoped create, edit, completion and deletion. Progress is calculated
from associated task completion in SQLite so it is not distorted by active task filters.
Dependency details include prerequisites and dependent tasks; incomplete prerequisites set the
blocked indicator, and the repository rejects self-links, cycles, missing tasks and cross-workspace
links. SQL filters cover milestone, branch, blocked state, overdue state, local/imported origin,
status, priority, completion, tags and search.

Manual time entries store duration, optional note and UTC record time. Adding or removing an entry
updates the task's tracked-minute aggregate and revision transactionally. History survives restart.
An active timer is deferred because Phase 4 requires safe monotonic timing, shutdown and crash
recovery semantics; no timer is presented in the UI.

## 7. Workspace and account isolation

Every application request, query and controller state carries an explicit active workspace ID.
Repository methods should require both the workspace ID and active account ID, and reject archived
or foreign-account workspaces for writes. The primary key is local and global, while all list,
summary, board, calendar, tag and import-dedup queries are workspace filtered. Composite
workspace foreign keys enforce same-workspace links.

On workspace change, clear the previous task model before loading the new workspace. Import reads
are asynchronous and generation-checked; a workspace switch cancels an active import. Session lock
clears source rows, preview and selections. Target inserts also reject archived and foreign-account
workspaces inside the transaction. Archive retains task rows with the workspace registration. On
lock/sign-out, clear the in-memory task model; the locked-session page hides Planner content.

## 8. Threading and concurrency strategy

Never pass a `QSqlDatabase`, `QSqlQuery` or repository holding a live Qt SQL connection between
threads. `InternalDatabaseManager::database()` is valid only on its owning thread, and the existing
Overview and service repositories use that main-thread connection. Moving it to `TaskScheduler`
workers would violate Qt SQL affinity.

The `SqlPlannerRepository` checks that each operation runs on `InternalDatabaseManager`'s owning
thread. Local `PlannerController` calls remain synchronous on that thread. Task list pages fetch
task rows and batch their tags, subtasks, dependency IDs, prerequisite details and dependent-task
details in chunks of at most 300 task IDs, avoiding one SQL round trip per relation per task. Full
task descriptions and relations are still loaded into the in-memory model. The controller currently
reads every matching page synchronously and retains the complete filtered workspace result, so
very large workspaces remain a GUI-thread and memory limit that needs target-machine profiling.
The import controller
reads one source task at a time on a scheduler worker under a consistent read transaction, then
queues the detached task to the owner thread for a per-task target transaction. The worker waits
with cancellation polling for that task boundary acknowledgement, so shutdown cannot strand a
blocking GUI call. Cancellation retains prior task commits and reports partial counts.

Use `revision` in update predicates (`WHERE id=? AND workspace_id=? AND revision=?`) and increment
it atomically. The repository's unique source index remains the final guard against repeated
imports even with another connection or process.

## 9. Error handling

Target validation and SQL failures use existing `Result<T>` and `AppError` patterns. Source failures
report a missing/unreadable profile, incompatible schema, busy database, changed profile, deleted
selection, invalid source data or cancellation. Show actionable messages inline in the Planner; do
not expose SQL text, task descriptions or source contents in logs/support bundles.

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

Use the existing Qt Test and CTest conventions, including temporary SQLite databases. Coverage in
`devhub_planner_tests`, `devhub_database_tests`, `devhub_somethingqt_import_tests`, and the existing
`devhub_something_tasks_reader_tests` includes:

- fresh migration to schema 12, v9 provenance upgrade, existing data preservation, rollback and
  foreign-key integrity;
- workspace/account isolation and archive/restore retention;
- CRUD, Unicode and a large description, tag replacement, subtask ordering, search and pagination;
- completion/status transitions, board ordering, revision conflicts and stale workspace reads;
- milestone CRUD/progress, dependency cycles, self/cross-workspace dependency rejection, and blocked-state changes;
- Git branch metadata and deleted-branch display, GitHub URL validation, manual time-entry history and restart persistence;
- SQL filters for branch, milestone, blocked, overdue, origin, status and priority;
- failed target transaction rollback, independent imported copies, 64-bit source IDs/revisions,
  long descriptions, 75 subtasks, Unicode tags, 1,000-task source streaming, live WAL reads, source
  deletion independence, malformed/missing profile handling and task-boundary cancellation;
- Assistant reader regression, paged Planner UI, runtime QML loading and migration preservation.

The opt-in `plannerListScaleProfile` test profiles 100, 1,000 and 10,000 task loads, Qt model reset,
in-memory search and board status filtering. It estimates task-model payload but does not measure
whole-process RSS or actual QML frame times.

## 12. Identified risks

1. **Two Overview implementations:** QML is active and `OverviewPage` is retained in the Widgets
   dialog stack. Keep the Planner in visible QML and test the main shell, or later shell work could
   expose a divergent Overview.
2. **Source profile identity:** the verified SomethingQt task schema has no stable profile UUID.
   DevHub hashes canonical path and file birth time; moving a profile changes identity and may allow
   another copy. The importer does not attempt fuzzy identity matching.
3. **Source schema drift:** task tables and validation may change. Validate the schema version and
   required affinities; reject incompatible inputs instead of guessing or truncating.
4. **Qt SQL affinity and concurrent app writes:** planner workers need independent connections to
   the same WAL database. Keep operations serialized within Planner, writes short, and handle
   `SQLITE_BUSY` and session cancellation.
5. **Import size and partial progress:** full profile import may take time. The reader streams one
   task at a time, shows progress, and commits each task atomically; rerunning skips completed copies.
6. **Status mapping:** SomethingQt only has a completion boolean. Map it deterministically to
   `todo`/`done`; other Planner states have no source equivalent.
7. **Data privacy:** local account locking does not encrypt SQLite. Keep content out of diagnostics
   and clear all cached state on lock and account change.
8. **QML vertical space:** the current shell is a scrollable Overview with a responsive card grid.
   The Planner must contribute accurate implicit height, scroll as one document and preserve access
   to the cards below it.
9. **Large workspaces:** task rows and relations are hydrated in batches, but the controller still
   synchronously loads all matching pages on the database-owning GUI thread. Results at 10,000 tasks
   are recorded in the Phase 6 verification section; the model payload is estimated, not RSS.

## 13. Implementation phases

1. **Phase 1 — local storage and services (implemented):** migration 0009, typed task/subtask/
   milestone values, validation and status rules, account/workspace-scoped SQLite repository,
   application service, optimistic task/milestone revisions, relations, filtering/search/sorting/
   pagination, and SQLite-backed tests. No UI or importer was added.
2. **Phase 2 — Overview UI (implemented):** add the controller/model and collapsible QML panel to
   the visible Overview, with board/list/calendar, task editing, filters and persistent ordering.
   Drag-and-drop remains deferred in favor of explicit persisted status/order controls.
3. **Phase 3 — SomethingQt import (implemented):** migration 0010, a dedicated read-only source
   reader, paged filtering, complete task previews, workspace-scoped duplicate detection, explicit
   independent copies, per-task atomic writes and cancellation at task boundaries. No source writes
   or synchronization are performed.
4. **Phase 4 — developer planning features (implemented):** migration 0011, reusable Git branch
   lookup and safe Git navigation, validated GitHub links, milestone progress and management,
   dependency detail/blocking, manual time-entry history, separate Planner notes and SQL metadata
   filters. Active timers remain deferred.
5. **Phase 5 — DevHub module integration (implemented):** migration 0012 adds saved-service links
   and bounded action history. Task details reuse Git, Notes, Services, Logs, workspace-folder,
   NotificationService, and Ctrl+K flows. Starting a saved service requires an explicit click and
   active workspace/session validation; task text is never executable. Planner data remains outside
   Assistant context until the disclosure and conversation-scope prerequisites are available.
6. **Phase 6 — release hardening (implemented):** review current Planner boundaries, batch relation
   reads for paged task results, avoid constructing subtask QVariant maps for unrelated model roles,
   profile 100/1,000/10,000 tasks, add a 1,000-task source-stream fixture, document schema 12 and
   import limits, and run the supported build/test presets available on the host.

## 14. Overall product acceptance criteria

- Planner content is visible in the existing main-window Overview after “Workspace Overview” and
  before all six original cards; it collapses and expands and adds no navigation entry or module.
- New and imported tasks share one local task model, use new DevHub IDs, survive source shutdown or
  source task deletion, and can be edited/deleted without touching SomethingQt.
- Source import preserves task fields, full descriptions, tag names and all subtasks with source IDs
  and positions, records task provenance, skips duplicates without overwriting local edits, and
  reports copies already committed when cancellation or a later failure stops the import.
- Every task and relation is scoped to one workspace and the active account; switching, archiving,
  locking and signing out do not leak stale task content or allow stale UI updates.
- CRUD target writes stay on the database owner's thread, and validation/conflict failures have
  user-visible handling.
- Summary, Kanban, list and calendar are projections of the same persisted task records; existing
  Overview cards remain unchanged below the Planner.
- Migrations are additive, transactional and rollback-safe, and documented implementation tests
  cover isolation, ownership, compatibility, cancellation and UI integration.

## Phase 3 completion (historical)

This section records the state at the end of Phase 3; Phase 4 implementation follows below.

The current app uses a Qt Quick main shell, Widgets feature pages in a secondary dialog, an
account-scoped internal QSQLITE database with immutable embedded migrations, a cooperative
`TaskScheduler`, and workspace-aware cancellation/archive lifecycle. Workspace Planner now appears
in the visible Overview as a QML child panel, with C++ application services and models exposed
through the existing shell; it does not enter `CurrentPageStack` or replace the six cards.

Phase 1 supplies local models and validation, migration 0009, account/workspace-scoped CRUD,
relations, filtering, sorting, pagination, completion rules, optimistic revisions and SQLite tests.
Phase 2 adds the embedded Planner panel, virtualized models, workspace-scoped view/collapse
settings, and controller tests. Service calls currently run synchronously on the database owner's
thread. At that point actual tracked time was a task total and branch references were free-form
metadata. The SomethingQt importer is an independent copy operation. Source tag IDs are not retained, and moving
a source file changes its hashed profile identity. Batch writes are atomic per task, so cancellation
or a later target failure leaves earlier completed task copies available.

## Phase 4 completion

Migration 0011 adds manual time-entry history with task/workspace foreign keys and query indexes.
Branch selection reads local branches through `GitService`; opening an association highlights the
branch in the existing Git module without checkout. Missing branches remain visible. GitHub link
validation distinguishes Issue and Pull Request paths and only opens supported HTTPS GitHub URLs
after an explicit action.

Milestones now have full create/edit/delete/status UI and database-calculated progress. Dependency
details show prerequisites and dependent tasks; blocked state updates as prerequisites complete.
Manual time entries update tracked totals transactionally and survive application restart. Planner
notes remain separate from descriptions, including on imported SomethingQt tasks. SQLite filtering
now covers milestone, branch, blocked, overdue, origin, status, priority, completion, tags and search.

No active timer was added. A running timer would need defined shutdown and crash recovery behavior;
Phase 4 delivers manual entries and persistent history without counting unobserved time.

## Phase 5 completion

Migration 0012 adds a workspace-scoped saved-service association and per-task action history. Each
task retains at most 20 finished runs plus an in-progress run; records contain a fixed action ID,
timestamps, result, duration, a 512-character error summary, and a bounded reference to the existing
service stdout source. Managed output stays in DevHub's existing capped Logs buffers.

Task details open Git, the repository folder, workspace Notes, the linked saved service, and its
related service logs. Starting the linked service uses `ServiceCoordinator` and a definition loaded
again from the active workspace. It is explicit, asynchronous, cancellable during startup, and
invalidated by workspace changes, account locking, archive, and shutdown. Failed starts create a
deduplicated Service notification. Ctrl+K includes Planner create, find, overdue, in-progress,
expand/collapse, and task-detail search actions.

Planner task disclosure to Assistant is not enabled. The missing prerequisites are explicit
disclosure consent, account/workspace/conversation scope binding, cancellation on scope changes, and
bounded treatment of untrusted task descriptions. See
[`workspace-planner-phase-5.md`](workspace-planner-phase-5.md) for supported and deferred workflows.

At the Phase 5 checkpoint on 2026-10-23, the Debug build completed all 423 Ninja steps and the
focused Planner, Planner UI, migration, SomethingQt import and QML runtime tests passed. The
26-target suite
had two incomplete long-running integration tests at that checkpoint: sandboxed Git for Windows
reported `NtCreateDirectoryObject` access denied during local push fixtures, and the elevated Git
test timed out at its then-current 90-second limit. Phase 6 raised the relevant test timeouts and
the final complete suite results are recorded below.

## Phase 6 verification and release status

Verified on 2026-10-23:

- `cmake --build --preset windows-dev-debug --parallel 4` completed successfully with Qt 6.11.0
  and MinGW 13.1.
- `ctest --test-dir out/build/windows-dev -C Debug --output-on-failure` passed all 26 targets in
  150 seconds. This includes the migration, Planner, Planner UI, SomethingQt import, QML runtime,
  authentication, account security, assistant, and existing module regression targets. Git-heavy
  integration tests use a 180-second CTest timeout; the final run completed `feature-regressions`
  in 87 seconds and `assistant-tool-runtime` in 32 seconds.
- The opt-in `plannerListScaleProfile` test passed with 100, 1,000 and 10,000 synthetic tasks.
  Debug measurements were respectively 7/53/722 ms for repository list loading, 0 ms for model
  reset at the timer's millisecond resolution, 0/0/10 ms for in-memory search, and 0/0/2 ms for
  board-status filtering. The estimated model payload at 10,000 tasks was 7.15 MiB; this is not
  whole-process memory usage or measured QML frame time.
- The 1,000-task SomethingQt stream fixture passed in 93 ms (about 10,752 tasks/second). The
  fixture checks task counts and content, a 256,000-character description, and the source database
  hash before and after reading. Broader import fixtures cover 64-bit IDs, Unicode, 75 subtasks,
  WAL reads, cancellation, duplicate handling and target rollback.
- `tools/package-windows.ps1 -PortableOnly` built a warnings-as-errors Release application and
  produced `out/release/DevHub-Portable-0.0.2-x64.zip`. Packaging staged Qt and QSQLITE and passed
  its isolated runtime check for SQLite, authentication views, the populated main shell, embedded
  Planner construction, and packaged runtime. Inno Setup is not installed, so no installer was
  produced.

The schema remains version 12; Phase 6 adds no migration. No real user database was reset or used
as a fixture. The MSVC `windows-ci-release` preset could not be verified because this host does not
have the required Visual Studio 2022 v143 14.44 toolset. The Release build was verified with the
supported MinGW package preset instead.

The automated QML runtime checks instantiate the shell and Planner, but the complete signed-in
acceptance sequence was not manually exercised: creating and editing tasks in the user's profile,
restarting between edits, importing from a real SomethingQt profile, switching/locking accounts and
workspaces, changing themes, resizing a small window, and keyboard-only navigation remain manual
acceptance gates. Consequently the implementation, Debug suite and portable Release package are
verified, but this is not a claim of full production release readiness. Before distribution, run
that interactive acceptance flow and the clean-machine, install/upgrade/uninstall and Qt license
review gates in [packaging/README.md](../../packaging/README.md).
