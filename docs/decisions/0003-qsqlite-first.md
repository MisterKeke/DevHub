# ADR 0003: Use Qt SQL with QSQLITE first

- Status: Accepted
- Date: 2026-09-28

## Context

DevHub needs an internal database and an initial read-only workspace SQLite browser. The master plan requires a replaceable backend boundary.

## Decision

Use Qt SQL and the QSQLITE driver first. Keep connection creation, sessions, schema reading, queries, and paging behind the interfaces specified by the master plan. Reconsider direct SQLite only when a documented limitation blocks a concrete requirement.

## Consequences

The project avoids an extra SQLite dependency and uses Qt’s supported SQL abstraction. Cancellation and low-level SQLite controls may be limited, so backend-specific details must not leak into UI or use cases.

## Alternatives considered

Direct SQLite C API was deferred until a demonstrated need for progress handlers, cancellation, authorizer callbacks, backup access, or exact statement metadata.
