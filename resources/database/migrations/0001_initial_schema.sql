-- Initial local DevHub database. Released migration scripts are immutable.
CREATE TABLE accounts (
    id TEXT NOT NULL PRIMARY KEY,
    username_normalized TEXT NOT NULL UNIQUE,
    display_username TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    password_scheme_version INTEGER NOT NULL DEFAULT 1 CHECK(password_scheme_version > 0),
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    last_login_at_utc TEXT
);
CREATE UNIQUE INDEX idx_accounts_single_account ON accounts((1));

CREATE TABLE application_settings (
    key TEXT NOT NULL PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
);
INSERT INTO application_settings(key, value) VALUES('persistent_query_history_enabled', 'false');

CREATE TABLE workspaces (
    id TEXT NOT NULL PRIMARY KEY,
    display_name TEXT NOT NULL,
    root_path TEXT NOT NULL,
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    is_favorite INTEGER NOT NULL DEFAULT 0 CHECK(is_favorite IN (0, 1))
);
CREATE UNIQUE INDEX idx_workspaces_root_path ON workspaces(root_path);

CREATE TABLE workspace_settings (
    workspace_id TEXT NOT NULL,
    key TEXT NOT NULL,
    value TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    PRIMARY KEY(workspace_id, key),
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE
);

CREATE TABLE workspace_detections (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    detected_path TEXT NOT NULL,
    detector_key TEXT NOT NULL,
    details_json TEXT NOT NULL DEFAULT '{}',
    observed_at_utc TEXT NOT NULL,
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    UNIQUE(workspace_id, detected_path, detector_key)
);
CREATE INDEX idx_workspace_detections_workspace ON workspace_detections(workspace_id, observed_at_utc DESC);

CREATE TABLE service_definitions (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    display_name TEXT NOT NULL,
    executable_path TEXT NOT NULL,
    arguments_json TEXT NOT NULL DEFAULT '[]',
    working_directory TEXT,
    auto_start INTEGER NOT NULL DEFAULT 0 CHECK(auto_start IN (0, 1)),
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE
);
CREATE INDEX idx_service_definitions_workspace ON service_definitions(workspace_id, display_name);

CREATE TABLE service_environment_references (
    service_id TEXT NOT NULL,
    variable_name TEXT NOT NULL,
    secret_reference TEXT NOT NULL,
    PRIMARY KEY(service_id, variable_name),
    FOREIGN KEY(service_id) REFERENCES service_definitions(id) ON DELETE CASCADE
);

CREATE TABLE log_sources (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    display_name TEXT NOT NULL,
    file_path TEXT NOT NULL,
    format_key TEXT NOT NULL DEFAULT 'text',
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE
);
CREATE INDEX idx_log_sources_workspace ON log_sources(workspace_id, display_name);

CREATE TABLE database_references (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    display_name TEXT NOT NULL,
    engine_key TEXT NOT NULL,
    database_path TEXT,
    host TEXT,
    port INTEGER,
    database_name TEXT,
    secret_reference TEXT,
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE
);
CREATE INDEX idx_database_references_workspace ON database_references(workspace_id, display_name);

CREATE TABLE note_nodes (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    parent_id TEXT,
    title TEXT NOT NULL,
    node_type TEXT NOT NULL DEFAULT 'note',
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    FOREIGN KEY(parent_id) REFERENCES note_nodes(id) ON DELETE CASCADE
);
CREATE INDEX idx_note_nodes_tree ON note_nodes(workspace_id, parent_id, sort_order);

CREATE TABLE note_tags (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    name TEXT NOT NULL,
    color_token TEXT,
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    UNIQUE(workspace_id, name)
);

CREATE TABLE note_tag_links (
    note_id TEXT NOT NULL,
    tag_id TEXT NOT NULL,
    PRIMARY KEY(note_id, tag_id),
    FOREIGN KEY(note_id) REFERENCES note_nodes(id) ON DELETE CASCADE,
    FOREIGN KEY(tag_id) REFERENCES note_tags(id) ON DELETE CASCADE
);
CREATE INDEX idx_note_tag_links_tag ON note_tag_links(tag_id, note_id);

CREATE TABLE recent_files (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    file_path TEXT NOT NULL,
    last_opened_at_utc TEXT NOT NULL,
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    UNIQUE(workspace_id, file_path)
);
CREATE INDEX idx_recent_files_workspace_opened ON recent_files(workspace_id, last_opened_at_utc DESC);

CREATE TABLE query_history (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    connection_reference_id TEXT,
    query_text TEXT NOT NULL,
    executed_at_utc TEXT NOT NULL,
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    FOREIGN KEY(connection_reference_id) REFERENCES database_references(id) ON DELETE SET NULL
);

CREATE TABLE notifications (
    id TEXT NOT NULL PRIMARY KEY,
    category TEXT NOT NULL,
    title TEXT NOT NULL,
    message TEXT NOT NULL,
    created_at_utc TEXT NOT NULL,
    read_at_utc TEXT,
    deduplication_key TEXT
);
CREATE INDEX idx_notifications_unread ON notifications(read_at_utc, created_at_utc DESC);

CREATE TABLE ui_state (
    state_key TEXT NOT NULL PRIMARY KEY,
    state_json TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
);
