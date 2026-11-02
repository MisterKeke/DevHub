-- Workspace-scoped many-to-many associations. Note metadata and Markdown remain
-- owned by Notes; the link table stores stable IDs only.
CREATE UNIQUE INDEX idx_note_nodes_workspace_id ON note_nodes(workspace_id, id);
CREATE UNIQUE INDEX idx_note_nodes_workspace_identity_kind ON note_nodes(workspace_id, id, node_type);

CREATE TABLE planner_task_note_links (
    workspace_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    note_id TEXT NOT NULL,
    note_kind TEXT NOT NULL DEFAULT 'note' CHECK(note_kind = 'note'),
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    PRIMARY KEY(workspace_id, task_id, note_id),
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    FOREIGN KEY(workspace_id, note_id, note_kind) REFERENCES note_nodes(workspace_id, id, node_type) ON DELETE CASCADE
);
CREATE INDEX idx_planner_task_note_links_note
    ON planner_task_note_links(workspace_id, note_id, created_at_utc);
