CREATE TABLE workspace_scan_state (
    workspace_id TEXT NOT NULL PRIMARY KEY,
    detector_version TEXT NOT NULL,
    scanned_at_utc TEXT NOT NULL,
    inspected_files INTEGER NOT NULL DEFAULT 0 CHECK(inspected_files >= 0),
    limit_reached TEXT NOT NULL DEFAULT '',
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE
);
