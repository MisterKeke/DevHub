# ADR 0004: Treat the local account as an app access boundary

- Status: Accepted
- Date: 2026-09-28

## Context

DevHub presents a local sign-in and lock flow while managing user-selected local files and processes.

## Decision

Use one local account in v1 and Argon2id password verification through libsodium. The account gates access through DevHub; it does not encrypt repositories, external databases, source files, logs, or notes. Sessions exist only in process memory and end at process exit.

## Consequences

The UI and documentation must state this boundary accurately. Password verification and future encryption-key management are separate systems.

The initial username policy is 1–64 Unicode code points, trimmed for display and compared after
NFKC normalization plus Unicode case folding. Account storage uses UUID identifiers rather than
usernames as filesystem names. Password hashing uses the libsodium Argon2id interactive profile;
measure its latency on representative release hardware before changing the cost profile.

## Alternatives considered

Encrypting all project data with the account password was rejected because DevHub does not own those files and must not imply filesystem encryption.
