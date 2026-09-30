# Workspace registry

DevHub stores workspace registrations as local metadata scoped to the signed-in `AccountId`.
The workspace display name is separate from both the path the user entered and the canonical root
used for duplicate detection. On Windows, canonical root comparisons are case-insensitive.

Adding or relocating a workspace validates that the selected path resolves to a readable directory.
Missing and inaccessible paths remain registered and are shown with a status so users can relocate
them. Removing a registration deletes only its DevHub metadata; project files are never removed.
Workspace metadata is committed before optional scans are introduced in later phases.

Activation validates the directory before updating `last_opened_at_utc` and recent ordering. The
workspace context cancels the previous context token on a switch and exposes a fresh token for work
belonging to the new workspace. Workspace listing and path inspection begin only after authentication.

The schema migration attaches legacy workspace rows to the local account when one exists and keeps
the original root path. It introduces entered and canonical paths, favorite state ordering, and the
last successful activation timestamp.
