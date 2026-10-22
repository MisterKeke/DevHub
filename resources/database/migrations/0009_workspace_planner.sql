-- Local, workspace-owned Planner records. Imported values are stored as provenance,
-- while planner_tasks contains the DevHub-owned editable state.
CREATE TABLE planner_tasks (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    title TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    is_completed INTEGER NOT NULL DEFAULT 0 CHECK(is_completed IN (0, 1)),
    status TEXT NOT NULL DEFAULT 'todo' CHECK(status IN ('backlog','todo','in_progress','in_review','done')),
    board_position INTEGER NOT NULL DEFAULT 0,
    created_at_utc TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL,
    due_date TEXT,
    start_date TEXT,
    priority TEXT NOT NULL DEFAULT 'medium' CHECK(priority IN ('low','medium','high')),
    difficulty TEXT NOT NULL DEFAULT '' CHECK(difficulty IN ('','easy','medium','hard')),
    revision INTEGER NOT NULL DEFAULT 1 CHECK(revision > 0),
    estimated_minutes INTEGER CHECK(estimated_minutes IS NULL OR estimated_minutes >= 0),
    actual_tracked_minutes INTEGER NOT NULL DEFAULT 0 CHECK(actual_tracked_minutes >= 0),
    milestone_id TEXT,
    git_branch TEXT,
    github_issue_url TEXT,
    github_pull_request_url TEXT,
    planner_notes TEXT NOT NULL DEFAULT '',
    blocked_reason TEXT NOT NULL DEFAULT '',
    source_type TEXT NOT NULL DEFAULT 'local' CHECK(source_type IN ('local','somethingqt')),
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    FOREIGN KEY(workspace_id, milestone_id) REFERENCES planner_milestones(workspace_id, id) ON DELETE RESTRICT,
    UNIQUE(workspace_id, id),
    CHECK(due_date IS NULL OR (length(due_date) = 10 AND date(due_date) = due_date)),
    CHECK(start_date IS NULL OR (length(start_date) = 10 AND date(start_date) = start_date)),
    CHECK((status = 'done') = (is_completed = 1))
);
CREATE INDEX idx_planner_tasks_workspace_status_board
    ON planner_tasks(workspace_id, status, board_position, created_at_utc);
CREATE INDEX idx_planner_tasks_workspace_due ON planner_tasks(workspace_id, due_date);
CREATE INDEX idx_planner_tasks_workspace_updated ON planner_tasks(workspace_id, updated_at_utc DESC);

CREATE TABLE planner_milestones (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    title TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    start_date TEXT,
    due_date TEXT,
    is_completed INTEGER NOT NULL DEFAULT 0 CHECK(is_completed IN (0, 1)),
    revision INTEGER NOT NULL DEFAULT 1 CHECK(revision > 0),
    created_at_utc TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL,
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    UNIQUE(workspace_id, id),
    CHECK(start_date IS NULL OR (length(start_date) = 10 AND date(start_date) = start_date)),
    CHECK(due_date IS NULL OR (length(due_date) = 10 AND date(due_date) = due_date))
);
CREATE INDEX idx_planner_milestones_workspace_due ON planner_milestones(workspace_id, due_date, title);

CREATE TABLE planner_tags (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    name TEXT NOT NULL,
    name_normalized TEXT NOT NULL,
    color_token TEXT,
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    UNIQUE(workspace_id, id),
    UNIQUE(workspace_id, name_normalized)
);
CREATE TABLE planner_task_tags (
    workspace_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    tag_id TEXT NOT NULL,
    PRIMARY KEY(task_id, tag_id),
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    FOREIGN KEY(workspace_id, tag_id) REFERENCES planner_tags(workspace_id, id) ON DELETE CASCADE
);
CREATE INDEX idx_planner_task_tags_tag ON planner_task_tags(workspace_id, tag_id, task_id);

CREATE TABLE planner_subtasks (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    source_subtask_id TEXT,
    title TEXT NOT NULL,
    is_completed INTEGER NOT NULL DEFAULT 0 CHECK(is_completed IN (0, 1)),
    position INTEGER NOT NULL DEFAULT 0,
    created_at_utc TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL,
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    UNIQUE(workspace_id, task_id, id)
);
CREATE INDEX idx_planner_subtasks_task_order ON planner_subtasks(workspace_id, task_id, position, id);

CREATE TABLE planner_task_dependencies (
    workspace_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    depends_on_task_id TEXT NOT NULL,
    created_at_utc TEXT NOT NULL,
    PRIMARY KEY(task_id, depends_on_task_id),
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    FOREIGN KEY(workspace_id, depends_on_task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    CHECK(task_id <> depends_on_task_id)
);
CREATE INDEX idx_planner_dependencies_target ON planner_task_dependencies(workspace_id, depends_on_task_id);

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
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    UNIQUE(workspace_id, source_type, source_profile_identity, source_task_id)
);
CREATE INDEX idx_planner_import_profile ON planner_import_records(workspace_id, source_profile_identity, source_task_id);

