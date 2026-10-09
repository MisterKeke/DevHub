CREATE TABLE assistant_conversations (
    id TEXT NOT NULL PRIMARY KEY,
    account_id TEXT NOT NULL,
    workspace_id TEXT,
    codex_thread_id TEXT NOT NULL,
    codex_home_key TEXT NOT NULL,
    codex_account_key TEXT NOT NULL,
    workspace_root_snapshot TEXT,
    title TEXT NOT NULL CHECK(length(title) <= 80),
    created_at_utc TEXT NOT NULL,
    updated_at_utc TEXT NOT NULL,
    last_opened_at_utc TEXT,
    last_turn_id TEXT,
    last_turn_status TEXT,
    compatibility_profile TEXT NOT NULL,
    tool_profile_hash TEXT,
    FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE,
    FOREIGN KEY(workspace_id) REFERENCES workspaces(id) ON DELETE SET NULL
);
CREATE UNIQUE INDEX idx_assistant_conversations_thread_scope
    ON assistant_conversations(account_id,codex_home_key,codex_account_key,codex_thread_id);
CREATE INDEX idx_assistant_conversations_account_updated
    ON assistant_conversations(account_id,updated_at_utc DESC);
