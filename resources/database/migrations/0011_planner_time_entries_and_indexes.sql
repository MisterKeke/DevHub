-- Persist manual time history and index the Phase 4 workspace filters.
CREATE TABLE planner_time_entries (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    duration_minutes INTEGER NOT NULL CHECK(duration_minutes > 0),
    note TEXT NOT NULL DEFAULT '',
    recorded_at_utc TEXT NOT NULL,
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    UNIQUE(workspace_id, task_id, id)
);
CREATE INDEX idx_planner_time_entries_task_time
    ON planner_time_entries(workspace_id, task_id, recorded_at_utc DESC, id);
CREATE INDEX idx_planner_tasks_workspace_branch
    ON planner_tasks(workspace_id, git_branch);
CREATE INDEX idx_planner_tasks_workspace_origin
    ON planner_tasks(workspace_id, source_type);
CREATE INDEX idx_planner_tasks_workspace_milestone
    ON planner_tasks(workspace_id, milestone_id, is_completed);
CREATE INDEX idx_planner_tasks_workspace_due_completion
    ON planner_tasks(workspace_id, is_completed, due_date);
CREATE INDEX idx_planner_tasks_workspace_priority_status
    ON planner_tasks(workspace_id, priority, status, board_position);
