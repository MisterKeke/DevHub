# SQLite Browser and Editing

The Database module registers workspace-scoped SQLite file references. It opens an existing file read-only by default, refuses to create a missing file, and runs each connection on its own worker thread. Schema inspection, row paging, counting, SQL execution, backups, and commits use that session worker. A page contains at most 500 rows; query results retain up to 10,000 rows and 16 MiB of displayed values.

The schema tab lists tables, views, indexes, and triggers. Selecting a table reads its column definitions, primary-key order, foreign-key metadata, and `WITHOUT ROWID` status. Row counts are only calculated when requested. Tables without declared primary keys remain browse-only for visual edits; offset paging can shift when another process writes to the same file.

SQL history is held in memory for the current application session, capped at 500 statements, and can be cleared. It is not persisted. Potentially mutating statements and scripts require confirmation. SQLite's query-only connection setting provides an additional barrier when the database is opened read-only.

Visual editing requires explicit activation on a table with a declared primary key. DevHub offers a separate `VACUUM INTO` backup before switching that session to read/write. Grid edits, inserts, and deletes stay pending until Commit Changes. Each commit is a transaction using prepared statements and primary-key predicates; a row count other than one causes the full transaction to roll back. Roll Back Pending discards the current grid changes.

Paging supports 25, 50, 100, 250, and 500 rows. Leaving a grid with pending changes, including switching workspaces, signing out, or closing DevHub, requires confirmation before discarding them. Grid input is disabled while a page or commit is in flight, and editing an existing cell updates the Commit Changes button immediately.

## Current limits

- SQL execution, counting, and paging use a native SQLite connection on the session worker. A progress callback supports Cancel SQL and enforces a ten-second execution deadline. Closing the session cancels these operations before joining the worker. Qt SQL continues to handle metadata, backups, and visual commits.
- BLOBs are shown as a size marker; preview and export actions are not implemented.
- Grid sorting is not exposed yet, though the worker accepts a quoted sort column.
- Query history persistence is intentionally absent; there is no setting that opts into storing SQL text.
- Mutating SQL asks for confirmation but does not automatically make a second backup. The visual edit-mode backup is separate and does not guarantee recovery from every failure.
- The Overview shows the saved database reference count and names. Database-specific CommandRegistry actions remain follow-up work.
