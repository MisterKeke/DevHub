# Workspace Planner database and import contract

## Ownership and scope

Planner data lives in DevHub's internal SQLite database. Every task, relation, milestone,
time entry, import record, and action run is scoped to a workspace. Repository reads and writes
also verify the workspace's owning account. Archiving a workspace only marks the workspace row;
its Planner data remains available if that workspace is restored. Permanent workspace deletion
cascades the workspace-owned rows through foreign keys.

DevHub assigns its own task and subtask IDs. Importing from SomethingQt creates an independent
copy and stores source identity as provenance. DevHub does not synchronize, refresh, or write back
to SomethingQt. Imported tasks remain editable in DevHub and usable after the source file is moved
or removed. The importer opens the source SQLite database read-only, does not run source migrations,
and does not checkpoint or change the source journal mode.

## Current tables

| Table | Purpose |
| --- | --- |
| `planner_tasks` | Editable DevHub task fields, planning metadata, status, optimistic revision, and optional saved-service ID. |
| `planner_tags`, `planner_task_tags` | Workspace-owned normalized tags and task associations. |
| `planner_subtasks` | Ordered, editable subtasks, including optional source subtask ID text. |
| `planner_milestones` | Workspace milestones and optimistic revision. |
| `planner_task_dependencies` | Same-workspace prerequisite edges; self-links and cross-workspace links are rejected. |
| `planner_import_records` | Source profile identity, full-width source task/revision text, original timestamps, local import time, and independent copy number. |
| `planner_time_entries` | Manual work duration history; task tracked time is its aggregate. |
| `planner_action_runs` | Bounded metadata for an explicitly started saved-service action; it does not duplicate service output. |

Titles, descriptions, notes, tags, and source IDs are stored as text without a character-count
truncation step. Source task IDs and revisions are read as SQLite integers and converted directly
to decimal text; they never pass through JSON floating point. The copy includes supported task
fields, tag names, all subtasks, source subtask IDs/positions, and original source timestamps.
SomethingQt tag IDs, normalized tag keys, and subtask creation timestamps are not exposed by its
task model and therefore are not copied. Source completion maps to `todo` or `done`; it has no
completion timestamp or intermediate status to import.

`linked_service_id` is validated in the application against a saved definition in the active
workspace. `planner_action_runs` currently accepts the allow-listed `service.start:<id>` action.
Completed history is capped at 20 rows per task, in addition to any in-progress run. Error summaries
are capped at 512 characters and log references identify the existing bounded service log source.

## Migration history

The current database schema version is **12**. Embedded migration files are immutable once
released and are applied transactionally by `InternalDatabaseManager`.

| Version | Migration | Change |
| --- | --- | --- |
| 9 | `0009_workspace_planner.sql` | Adds tasks, tags, subtasks, milestones, dependencies, and import provenance with workspace foreign keys and query indexes. |
| 10 | `0010_planner_import_copies.sql` | Rebuilds the provenance uniqueness key to support separately requested independent copies while preserving existing provenance as copy 1. |
| 11 | `0011_planner_time_entries_and_indexes.sql` | Adds manual time entries and indexes for branch, origin, milestone, due-date, priority/status, and time-history queries. |
| 12 | `0012_planner_service_actions.sql` | Adds saved-service association and per-task action-run history. |

Fresh-install, existing-installation upgrade, provenance preservation, foreign-key checks,
transaction rollback, refusal of a newer schema, and corruption handling are covered by
`devhub.database-migrations` and `devhub.planner`. Tests use temporary databases and do not reset
the user's DevHub database.

## SomethingQt copy behavior

The importer pages source summaries at 50 tasks per page and streams selected full task payloads
one at a time. Each accepted task is committed in its own target transaction. A later error or
cancellation preserves earlier completed copies and reports partial counts. A duplicate profile
and task is skipped by default; an explicit independent-copy choice increments its copy number.
Import into another workspace creates a separate local copy. Source identity is derived from the
canonical path and file birth time because SomethingQt does not expose a stable profile UUID; moving
the profile may therefore permit a second import identity.

Unsupported or malformed source values fail that task instead of being silently shortened or
coerced. The importer reports missing, unreadable, busy, replaced, incompatible, and removed
sources. The source database is never modified, and no imported title, description, branch, or URL
is executed automatically.

## Verification and performance profile

Use the repository's CMake presets from a configured Windows developer environment:

```powershell
cmake --build --preset windows-dev-debug --parallel
ctest --preset windows-dev-debug
```

The opt-in Planner scale profile seeds a temporary database and measures paged repository loading,
Qt list-model reset, in-memory search filtering, board status filtering, and estimated task-model
payload at 100, 1,000, and 10,000 tasks:

```powershell
$env:DEVHUB_RUN_PLANNER_PERF = '1'
ctest --test-dir out/build/windows-dev -C Debug -R '^devhub\.planner$' --verbose
Remove-Item Env:DEVHUB_RUN_PLANNER_PERF
```

The payload estimate is not whole-process RSS; use Windows Performance Recorder or Task Manager
for machine-level memory profiling. It includes the task struct and title/description storage, but
not Qt container, relation, allocator or QML overhead. The QML delegates use `ListView` virtualization,
but the current controller keeps the active workspace's full filtered result in memory and performs
database list work on the database-owning GUI thread. Large-workspace timing should be recorded for
the target machine before setting release performance claims. Search reload is debounced; the importer runs
source reads on the bounded task scheduler and commits one task at a time on the database owner.

The Qt tests cover storage, controller, QML runtime, importer, lifecycle and module regressions.
They do not replace a manual signed-in GUI acceptance run covering keyboard navigation, small-window
layout, theme switching, startup/shutdown and persistent data across an actual application restart.
