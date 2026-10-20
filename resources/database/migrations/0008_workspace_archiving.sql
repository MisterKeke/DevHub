-- Archive registrations without deleting workspace-owned records.
ALTER TABLE workspaces ADD COLUMN archived_at_utc TEXT;
CREATE INDEX idx_workspaces_account_archived
ON workspaces(account_id, archived_at_utc, display_name COLLATE NOCASE);
