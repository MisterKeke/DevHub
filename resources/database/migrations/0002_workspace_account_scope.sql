-- Keep legacy workspace paths while making ownership and canonical roots explicit.
ALTER TABLE workspaces ADD COLUMN account_id TEXT REFERENCES accounts(id) ON DELETE CASCADE;
ALTER TABLE workspaces ADD COLUMN entered_path TEXT NOT NULL DEFAULT '';
ALTER TABLE workspaces ADD COLUMN canonical_root_path TEXT NOT NULL DEFAULT '';
ALTER TABLE workspaces ADD COLUMN recent_order INTEGER NOT NULL DEFAULT 0;
ALTER TABLE workspaces ADD COLUMN last_opened_at_utc TEXT;
UPDATE workspaces SET entered_path = root_path, canonical_root_path = root_path;
UPDATE workspaces SET account_id = (SELECT id FROM accounts ORDER BY created_at_utc LIMIT 1)
WHERE account_id IS NULL AND EXISTS (SELECT 1 FROM accounts);
DROP INDEX IF EXISTS idx_workspaces_root_path;
CREATE INDEX idx_workspaces_account_recent ON workspaces(account_id, is_favorite DESC, recent_order, display_name COLLATE NOCASE);
CREATE UNIQUE INDEX idx_workspaces_account_canonical_path
ON workspaces(account_id, canonical_root_path) WHERE account_id IS NOT NULL;
