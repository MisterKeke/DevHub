# Internal database and migrations

DevHub’s private QSQLITE database lives at `LocalAppData/DevHub/data/devhub.db`. Workspace
SQLite files are separate external data sources and are not managed by this database.

`InternalDatabaseManager` owns one uniquely named Qt SQL connection. All connection operations
and connection removal are constrained to the manager’s QObject thread. Every connection
enables SQLite foreign keys and a 5000 ms busy timeout. WAL is requested and verified. Existing
databases receive `PRAGMA quick_check(1)` before schema changes; a failed integrity check leaves
the file intact and enters recovery mode.

Migration scripts are embedded in `resources/database_migrations.qrc` under
`:/database/migrations`. Names use `NNNN_lower_snake_case.sql`, versions are unique positive
integers, and scripts are sorted by version. Released scripts are immutable. Each script and
its schema-ledger row are applied in one transaction. `SqlTransactionGuard` rolls back unless
`commit()` succeeds. A schema version beyond the newest bundled migration is refused.

Mark an update that needs a safety copy with a leading SQL comment:

```sql
-- devhub:backup-required
```

Before running a marked migration, SQLite `VACUUM INTO` makes a consistent database copy in the
backups directory. DevHub keeps the ten newest `internal-pre-migration-*.db` copies. The recovery
screen has a separate “Copy database aside” action that preserves the current database under a
unique timestamped filename before retry or exit.

Migration versions currently create account metadata, application and workspace settings,
workspace detections, service definitions and secret references, log sources, database
references, note metadata, recent files, query-history metadata, notifications, and UI state.
Persistent query history is disabled by default. Indexes are limited to uniqueness constraints,
foreign-key cleanup, and expected workspace/tree/recent/unread listing paths.

The CTest database suite covers clean initialization, already-current databases, rollback,
newer-schema refusal, and corruption preservation. Use `ctest --preset windows-dev-debug` to run
it after configuring a test-enabled build.
