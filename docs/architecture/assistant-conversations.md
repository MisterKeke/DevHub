# Assistant conversation metadata and restoration

Phase 5 adds `assistant_conversations` to the internal SQLite database. The
table stores the DevHub UUID and account, optional workspace ID/root snapshot,
local title, UTC created/updated/opened timestamps, Codex thread ID, last turn
status, and compatibility/profile fingerprints. Codex home is stored as a
SHA-256 identity key. Message bodies, credentials, email addresses and raw
Codex rollout files are never copied into DevHub storage.

`SqlConversationRepository` uses prepared statements on the internal database's
owning thread. Every list and write is account-scoped. Writes validate the
workspace belongs to that local account; deleting a workspace sets the foreign
key to null while retaining the association and its root snapshot for context
comparison. The snapshot does not grant workspace access. Deleting local
metadata does not delete Codex history.

The controller saves a local association only after `thread/start` returns a
validated Codex thread. It then updates metadata after a terminal turn. SQL
errors are surfaced while the live chat continues. User messages admitted after
thread creation are recoverable from Codex history; DevHub does not replay them
automatically.

When `assistant/restoreConversation` is enabled, startup considers at most the
most recently updated DevHub association for the signed-in local account. It
attempts `thread/resume` only when the connected Codex account fingerprint and
validated runtime profile match. The Codex client additionally validates the
saved Codex-home key, stable isolated cache working directory, thread ID, model
provider, history mode and capability restrictions. No Codex thread listing or
same-title search is used. A removed thread, changed account/home, incompatible
history or profile mismatch leaves the local association intact and presents
New Chat guidance.

Hydration rebuilds the visible transcript from the returned thread once. It
includes only user and assistant text items, filters internal reasoning and
other item types, and applies existing history, per-item, transcript and
message-count caps. It replaces view state instead of appending, so messages
are not duplicated. The local title and timestamps remain DevHub metadata;
accepted message content remains Codex history.

Accepted failed or interrupted turns use the existing explicit history check
before a user can choose to send the source again as a new turn. An uncertain
send is never automatically replayed. Disabling restoration stops new metadata
writes and automatic resume; it does not erase existing associations or promise
deletion of Codex-owned history. DevHub database backups include the metadata
table, not transcript bodies.
