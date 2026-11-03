-- Workspace and local-account scoped Build & Test profiles and bounded run history.
CREATE TABLE build_profiles (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    account_id TEXT NOT NULL,
    name TEXT NOT NULL,
    runner INTEGER NOT NULL,
    trusted INTEGER NOT NULL DEFAULT 0 CHECK(trusted IN (0,1)),
    settings_json TEXT NOT NULL DEFAULT '{}',
    created_at_utc TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL,
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    UNIQUE(workspace_id,id)
);
CREATE INDEX idx_build_profiles_workspace_name ON build_profiles(workspace_id,name);

CREATE TABLE build_execution_runs (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    account_id TEXT NOT NULL,
    profile_id TEXT NOT NULL,
    profile_name TEXT NOT NULL,
    runner INTEGER NOT NULL,
    status INTEGER NOT NULL,
    started_at_utc TEXT NOT NULL,
    finished_at_utc TEXT,
    duration_ms INTEGER NOT NULL DEFAULT 0 CHECK(duration_ms >= 0),
    exit_code INTEGER NOT NULL DEFAULT -1,
    summary TEXT NOT NULL DEFAULT '',
    log_reference TEXT NOT NULL DEFAULT '',
    task_id TEXT,
    is_test INTEGER NOT NULL DEFAULT 0 CHECK(is_test IN (0,1)),
    total_tests INTEGER NOT NULL DEFAULT 0 CHECK(total_tests >= 0),
    passed_tests INTEGER NOT NULL DEFAULT 0 CHECK(passed_tests >= 0),
    failed_tests INTEGER NOT NULL DEFAULT 0 CHECK(failed_tests >= 0),
    skipped_tests INTEGER NOT NULL DEFAULT 0 CHECK(skipped_tests >= 0),
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE,
    UNIQUE(workspace_id,id)
);
CREATE INDEX idx_build_runs_workspace_time ON build_execution_runs(workspace_id,started_at_utc DESC,id DESC);

CREATE TABLE build_test_results (
    id TEXT NOT NULL PRIMARY KEY,
    workspace_id TEXT NOT NULL,
    run_id TEXT NOT NULL,
    package_name TEXT NOT NULL DEFAULT '',
    test_name TEXT NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('passed','failed','skipped')),
    duration_ms INTEGER NOT NULL DEFAULT 0 CHECK(duration_ms >= 0),
    details TEXT NOT NULL DEFAULT '',
    FOREIGN KEY(workspace_id,run_id) REFERENCES build_execution_runs(workspace_id,id) ON DELETE CASCADE,
    UNIQUE(workspace_id,run_id,id)
);
CREATE INDEX idx_build_test_results_run ON build_test_results(workspace_id,run_id,status,test_name);
