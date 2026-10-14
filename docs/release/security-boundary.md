# Security boundary

DevHub is a local desktop application. Its local account password gates access to DevHub's account and workspace metadata; it does not encrypt workspace files, external SQLite databases, service processes, or other files on the computer.

The application stores internal metadata, settings, logs, notes, and recovery material under the current Windows user's local application-data directory. Support bundles are generated only at a path the user selects. Source files, external database contents, and note bodies are excluded from support bundles.

DevHub does not request administrator privileges. Service definitions can launch programs with the current user's privileges. Shell mode is opt-in for a service definition. The Port Monitor can terminate selected processes only after confirmation and rechecks process identity.

Users remain responsible for Windows account security, disk protection, backups, and third-party program trust. Managed services run with the user's privileges and inherit the full DevHub environment; the saved environment-name list is metadata and does not restrict or validate variables. On Windows DevHub owns each service process tree in a Job Object and closes that job during stop/restart or root-process exit. The local account password is not a Windows credential and is not sent to a server.

Assistant uses Codex-managed ChatGPT authorization, separate from DevHub's local
password. DevHub requests no API key and does not read or persist credential
files. Its settings store only executable/model preferences, excluded from
support settings exports. Authorization URL/device-code challenges are transient
and clear on cancellation, expiry, completion and local session revocation.
Opening the validated authorization page requires an explicit button click;
ordinary chat stays in DevHub. Completing authorization can change Codex's shared
account used by other applications. Sign-in and the model catalog do not bypass
the native text/scope safety gate. See [Assistant setup](../architecture/assistant-settings.md).

DevHub restores only thread IDs it previously associated in its own account-
scoped database. Each association is checked against the current Codex account,
Codex home fingerprint, compatibility profile, and DevHub workspace binding.
The Codex working directory is a stable isolated cache subdirectory; this path is
not a workspace grant. Local conversation metadata does not include message
bodies. Disabling `assistant/restoreConversation` prevents new association
saves and automatic restore without erasing existing metadata or Codex history.
