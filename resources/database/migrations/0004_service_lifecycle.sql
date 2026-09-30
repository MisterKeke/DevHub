ALTER TABLE service_definitions ADD COLUMN shell_mode INTEGER NOT NULL DEFAULT 0 CHECK(shell_mode IN (0, 1));
ALTER TABLE service_definitions ADD COLUMN expected_ports_json TEXT NOT NULL DEFAULT '[]';
ALTER TABLE service_definitions ADD COLUMN environment_names_json TEXT NOT NULL DEFAULT '[]';
ALTER TABLE service_definitions ADD COLUMN restart_policy TEXT NOT NULL DEFAULT 'never';
ALTER TABLE service_definitions ADD COLUMN stop_timeout_ms INTEGER NOT NULL DEFAULT 5000 CHECK(stop_timeout_ms BETWEEN 100 AND 120000);
CREATE UNIQUE INDEX idx_service_definitions_workspace_name ON service_definitions(workspace_id, display_name COLLATE NOCASE);
