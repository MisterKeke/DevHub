-- Preserve default duplicate skipping while allowing separately requested copies.
ALTER TABLE planner_import_records RENAME TO planner_import_records_v9;
CREATE TABLE planner_import_records (
    task_id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    source_type TEXT NOT NULL CHECK(source_type = 'somethingqt'),
    source_profile_identity TEXT NOT NULL,
    source_task_id TEXT NOT NULL,
    source_revision TEXT,
    source_created_at TEXT,
    source_updated_at TEXT,
    imported_at_utc TEXT NOT NULL,
    copy_number INTEGER NOT NULL DEFAULT 1 CHECK(copy_number > 0),
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    UNIQUE(workspace_id, source_type, source_profile_identity, source_task_id, copy_number)
);
INSERT INTO planner_import_records(
    task_id, workspace_id, source_type, source_profile_identity, source_task_id,
    source_revision, source_created_at, source_updated_at, imported_at_utc, copy_number)
SELECT task_id, workspace_id, source_type, source_profile_identity, source_task_id,
       source_revision, source_created_at, source_updated_at, imported_at_utc, 1
FROM planner_import_records_v9;
DROP TABLE planner_import_records_v9;
CREATE INDEX idx_planner_import_profile
    ON planner_import_records(workspace_id, source_profile_identity, source_task_id, copy_number);
