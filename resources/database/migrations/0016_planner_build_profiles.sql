-- Tasks may reference an explicitly selected Build & Test profile in the same workspace.
ALTER TABLE planner_tasks ADD COLUMN linked_build_profile_id TEXT;
ALTER TABLE planner_tasks ADD COLUMN linked_test_profile_id TEXT;
CREATE INDEX idx_planner_tasks_build_profile ON planner_tasks(workspace_id,linked_build_profile_id);
CREATE INDEX idx_planner_tasks_test_profile ON planner_tasks(workspace_id,linked_test_profile_id);
