# Backup and recovery

DevHub's internal database and local profile are stored under Windows Local AppData. Workspace files remain in their selected folders. Back up both the DevHub Local AppData folder and any workspace folders that matter to you.

Before internal database migrations, DevHub creates a local pre-migration database copy. If startup detects database damage, use Recovery Mode's **Copy database aside** action before retrying. Do not delete the internal database to clear a migration error; preserve a copy and report the displayed migration/version details.

The internal database includes Assistant conversation associations: local titles,
UTC timestamps, optional workspace identity/root snapshots, and opaque Codex
thread/account/home/profile fingerprints. It does not contain Assistant message
bodies. Restored message history comes from Codex. Removing a DevHub association,
resetting the local profile, or deleting a database backup does not delete the
corresponding Codex history. Back up the Codex-managed data separately if it
matters to you.

An unexpected-exit notice explains that Git and SQL operations are not replayed and managed services are not restarted. Check the Ports page for processes that may still be running. When a note with a newer recovery journal is opened, DevHub offers restore, compare, or discard choices.

The installer preserves Local AppData during upgrades and normal uninstall. The uninstaller offers a separate, explicit choice to remove local DevHub data. Keep an independent backup before accepting that deletion.
