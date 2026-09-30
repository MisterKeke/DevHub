ALTER TABLE note_nodes ADD COLUMN is_pinned INTEGER NOT NULL DEFAULT 0 CHECK(is_pinned IN (0, 1));
ALTER TABLE note_nodes ADD COLUMN is_trashed INTEGER NOT NULL DEFAULT 0 CHECK(is_trashed IN (0, 1));
ALTER TABLE note_nodes ADD COLUMN content_hash TEXT NOT NULL DEFAULT '';
CREATE INDEX idx_note_nodes_workspace_trash_pin ON note_nodes(workspace_id, is_trashed, is_pinned DESC, sort_order, title COLLATE NOCASE);
