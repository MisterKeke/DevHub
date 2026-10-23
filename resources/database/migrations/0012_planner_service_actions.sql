-- Bind tasks to existing workspace services and keep bounded execution metadata.
ALTER TABLE planner_tasks ADD COLUMN linked_service_id TEXT NOT NULL DEFAULT '';

CREATE TABLE planner_action_runs (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    action_id TEXT NOT NULL,
    started_at_utc TEXT NOT NULL,
    finished_at_utc TEXT,
    succeeded INTEGER CHECK(succeeded IS NULL OR succeeded IN (0, 1)),
    duration_ms INTEGER NOT NULL DEFAULT 0 CHECK(duration_ms >= 0),
    error_summary TEXT NOT NULL DEFAULT '',
    log_reference TEXT NOT NULL DEFAULT '',
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    CHECK((finished_at_utc IS NULL AND succeeded IS NULL) OR
          (finished_at_utc IS NOT NULL AND succeeded IS NOT NULL)),
    UNIQUE(workspace_id, task_id, id)
);

CREATE INDEX idx_planner_action_runs_task_start
    ON planner_action_runs(workspace_id, task_id, started_at_utc DESC, id);
