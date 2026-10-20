# Workspace archiving

Removing a workspace from the normal DevHub list archives its registration. The workspace ID, account ownership, canonical path, settings, service and database definitions, log sources, notes, and assistant conversation associations stay in the internal database. Note content files and the project folder remain where they are. Restoring reactivates the original registration and ID.

Permanent forgetting is intentionally not available. A future permanent deletion feature must define and verify cleanup for note content and recovery files, workspace and newer application settings, notifications, references and cached state, assistant conversations and retained transcripts, and all other records keyed by workspace ID. It must also handle account scoping, file deletion failures, and foreign-key dependents before removing the registration.
