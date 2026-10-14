# SQLite Browser and Editing

The Database module registers workspace-scoped SQLite file references. It opens an existing file read-only by default, refuses to create a missing file, and runs each connection on its own worker thread. Schema inspection, row paging, counting, SQL execution, backups, and commits use that session worker. A page contains at most 500 rows; query results retain up to 10,000 rows and 16 MiB of displayed values. User-selected connections enable SQLite defensive mode and disable trusted schemas while retaining normal SQL, trigger, view, backup, and visual-edit operations.

The schema tab lists tables, views, indexes, and triggers. Selecting a table reads its column definitions, primary-key order, foreign-key metadata, and `WITHOUT ROWID` status. Row counts are only calculated when requested. Tables without declared primary keys remain browse-only for visual edits; offset paging can shift when another process writes to the same file.

SQL history is held in memory for the current application session, capped at 500 statements, and can be cleared. It is not persisted. Potentially mutating statements and scripts require confirmation. SQLite's query-only connection setting provides an additional barrier when the database is opened read-only.

Visual editing requires explicit activation on a table with a declared primary key. DevHub offers a separate `VACUUM INTO` backup before switching that session to read/write. Grid edits, inserts, and deletes stay pending until Commit Changes. Each commit is an immediate transaction using prepared statements. Deletes compare a fingerprint of the original row state with the current row before removing it; changed or missing rows cause the full transaction to roll back. A concurrent external writer is held off during validation and commit. Roll Back Pending discards the current grid changes.

Paging supports 25, 50, 100, 250, and 500 rows. Leaving a grid with pending changes, including switching workspaces, signing out, or closing DevHub, requires confirmation before discarding them. Grid input is disabled while a page or commit is in flight, and editing an existing cell updates the Commit Changes button immediately.

For text columns, typing `NULL` stores literal text. For NUMERIC columns, typing
`NULL` (any letter case) stages SQL NULL; empty text remains empty text. The
context-menu **Set Cell to SQL NULL** command explicitly stages SQL NULL for any
supported column. NUMERIC integer text is stored as a signed 64-bit integer when
in range, preserving values above 2^53. Decimal/exponent input uses SQLite REAL
semantics. Integer text outside SQLite's signed 64-bit range and non-finite
numeric values are rejected rather than rounded. SQL NULL displays as italic
`(SQL NULL)` and uses the normal pending-change workflow.

## Current limits

- Project database sessions use one vcpkg native SQLite 3.53.4 connection on the session worker for opening, metadata, SQL execution, counting, paging, backups, and visual commits. The connection enables defensive mode and disables trusted schemas. Qt SQL remains responsible for DevHub's internal profile database; its bundled SQLite handles never cross into the native engine.
- Run Query accepts exactly one SQLite-prepared statement. Run Script prepares statements using SQLite's parser, preserving comments and trigger bodies. Scripts do not get an implicit transaction: valid statements before a later runtime error, cancellation, or execution limit may already be committed. A syntax/preparation error is found before script execution begins. Add explicit `BEGIN`/`COMMIT` when an all-or-nothing script is needed. A progress callback supports Cancel SQL and enforces a ten-second execution deadline.
- BLOBs are shown as a size marker; preview and export actions are not implemented.
- Grid sorting is not exposed yet, though the worker accepts a quoted sort column.
- Query history persistence is intentionally absent; there is no setting that opts into storing SQL text.
- Mutating SQL asks for confirmation but does not automatically make a second backup. The visual edit-mode backup is separate and does not guarantee recovery from every failure.
- The Overview shows the saved database reference count and names. Database-specific CommandRegistry actions remain follow-up work.
