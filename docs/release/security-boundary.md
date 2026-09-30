# Security boundary

DevHub is a local desktop application. Its local account password gates access to DevHub's account and workspace metadata; it does not encrypt workspace files, external SQLite databases, service processes, or other files on the computer.

The application stores internal metadata, settings, logs, notes, and recovery material under the current Windows user's local application-data directory. Support bundles are generated only at a path the user selects. Source files, external database contents, and note bodies are excluded from support bundles.

DevHub does not request administrator privileges. Service definitions can launch programs with the current user's privileges. Shell mode is opt-in for a service definition. The Port Monitor can terminate selected processes only after confirmation and rechecks process identity.

Users remain responsible for Windows account security, disk protection, backups, third-party program trust, and review of service executable paths and environment-variable names. The local account password is not a Windows credential and is not sent to a server.
