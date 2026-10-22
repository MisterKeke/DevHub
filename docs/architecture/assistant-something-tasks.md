# SomethingQt task lookup

DevHub exposes two read-only assistant tools backed by one explicitly selected
SomethingQt profile database. The connection path uses DevHub's global,
sensitive setting scope, while task-data disclosure is cleared when the active
DevHub account changes. The default path shown in the Assistant page is only a suggestion:
`%LOCALAPPDATA%/Something/Something/profiles/default/something.sqlite`. The user
selects an existing file and may run a connection check from the Assistant page;
DevHub does not check the database during startup.

The profile reader is a short-lived adapter. Each check, search or detail read
opens a uniquely named `QSQLITE` connection on the calling scheduler worker,
requires an existing regular readable file, enables SQLite read-only open mode,
uses ordinary SQLite WAL handling and closes the connection on that same
worker. It never uses immutable mode, copies the main database, changes journal
mode, checkpoints WAL, migrates, or accesses SomethingQt's application lock.
Qt SQL connections never cross worker boundaries. Each operation validates the
schema inside a short read transaction, so replacing the configured file causes
the next operation to validate the replacement before using it.

## Supported read contract

The required tables and columns are intentionally narrow:

- `schema_migrations(version INTEGER, name TEXT)` with a contiguous history
  beginning at v1, whose name is `core tasks currencies preferences`.
- `tasks(id, title, description, is_completed, created_at, updated_at,
  due_date, priority, difficulty, revision)`.
- `tags(id, name, name_normalized)` and `task_tags(task_id, tag_id)`.
- `task_subtasks(id, task_id, title, is_completed, position)`.

Column SQLite affinities must match the contract. Later migration entries,
additional columns, tables, indexes and triggers are accepted. Missing or
incompatible required structures return an actionable schema error. Ledger
history is read-only and no migration is attempted.

`devhub_something_tasks_find` accepts one to three exact tags, an optional
completion filter (`all`, `complete`, or `incomplete`), an optional page size
from 1 to 20, and an offset from 0 to 100,000. A workspace default tag resolves
an omitted tag. The optional workspace restriction adds its configured tag to
every search and requires it on every detail read. Defaults are convenience;
only the separate restriction switch enforces a workspace tag. Tags are trimmed
and case-folded with Qt semantics, compared by equality, and use AND semantics.

Search results order by `updated_at DESC, id DESC`; IDs and revisions are JSON
strings to preserve SQLite integer precision. Detail reads return bounded text
and at most 50 subtasks, with explicit truncation markers. Due dates remain
calendar-date strings. Each page is a fresh read, not a stable multi-page
snapshot; concurrent edits can shift offset pages. Busy databases and missing
WAL access fail with retryable/actionable statuses.

The full serialized tool result is limited to 48 KiB for searches and 40 KiB
for detail reads. Oversized results shorten text at UTF-16 character boundaries
and omit whole displayed tags, preserving task IDs, task metadata, source,
requested tags and retrieval time. Field and response truncation flags describe
these changes, including shortened subtask titles. If the byte budget requires
fewer search rows, `hasMore` is true and `nextOffset` resumes after the returned
rows; otherwise the next offset is the current offset plus the returned count.
The same result supplies UI status and the assistant response. Revoked or
cancelled requests cannot publish lookup or connection status in a later scope.

## Disclosure and scope

The Assistant page has a separate per-chat task-data disclosure checkbox. Task
titles, descriptions, tags and subtasks are sent to the assistant only while this
grant is active. Lock/sign-out, account/workspace/conversation changes, profile
changes and disclosure changes cancel pending work. Grants are rechecked before
the worker starts and before a result is published. Retrieved content is marked
as untrusted data and cannot authorize other tools or actions. Task data is not
included in diagnostics or scope fingerprints; the binding contains only hashes
of connection/tag/grant identity.

The Assistant page displays the selected profile identity, connection status,
requested tag, retrieval time, empty/error states and truncation. Nothing in this
integration creates or mutates tasks or changes SomethingQt files.

Synthetic reader regression tests cover exact and Unicode tag matching,
whitespace, AND filters, completion, subtasks, long descriptions, large IDs,
offset ordering, schema replacement, cancellation, permissions and busy
handling. The Planner import uses its own `SomethingQtImportReader`: it shares
the read-only schema contract but has no Assistant response caps, and it does
not use or change the Assistant disclosure grant. Phase 3 regression coverage
builds compatible SQLite fixtures and verifies the Assistant reader remains
unchanged.
