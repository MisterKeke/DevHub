CREATE TABLE app_settings (
    scope TEXT NOT NULL CHECK(scope IN ('global', 'workspace')),
    workspace_id TEXT NOT NULL DEFAULT '',
    setting_key TEXT NOT NULL,
    value_json TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL,
    PRIMARY KEY(scope, workspace_id, setting_key),
    CHECK((scope = 'global' AND workspace_id = '') OR (scope = 'workspace' AND workspace_id <> ''))
);
-- Migration 0001 already created notifications using the original read_at_utc
-- and text category fields. Upgrade that table in place so existing notices
-- survive and migration 0006 works on both fresh and previously used profiles.
ALTER TABLE notifications ADD COLUMN severity INTEGER NOT NULL DEFAULT 0;
ALTER TABLE notifications ADD COLUMN workspace_id TEXT;
ALTER TABLE notifications ADD COLUMN is_read INTEGER NOT NULL DEFAULT 0 CHECK(is_read IN (0, 1));
ALTER TABLE notifications ADD COLUMN repeat_count INTEGER NOT NULL DEFAULT 1 CHECK(repeat_count > 0);

UPDATE notifications
SET is_read = CASE WHEN read_at_utc IS NULL OR read_at_utc = '' THEN 0 ELSE 1 END;

UPDATE notifications
SET category = CASE lower(category)
    WHEN 'workspace' THEN '1'
    WHEN 'service' THEN '2'
    WHEN 'port' THEN '3'
    WHEN 'git' THEN '4'
    WHEN 'database' THEN '5'
    WHEN 'security' THEN '6'
    ELSE '0'
END;

-- Keep all historical rows while making existing duplicate keys unique before
-- adding the index required for notification aggregation.
UPDATE notifications AS current
SET deduplication_key = current.deduplication_key || '#legacy-' || current.id
WHERE current.deduplication_key IS NOT NULL
  AND EXISTS (
      SELECT 1 FROM notifications AS earlier
      WHERE earlier.deduplication_key = current.deduplication_key
        AND earlier.rowid < current.rowid
  );

CREATE INDEX idx_notifications_center ON notifications(is_read, created_at_utc DESC);
CREATE INDEX idx_notifications_workspace ON notifications(workspace_id, created_at_utc DESC);
CREATE UNIQUE INDEX idx_notifications_dedup_key ON notifications(deduplication_key);
