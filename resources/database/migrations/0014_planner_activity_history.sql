-- Append-only task activity and stable commit associations. All rows cascade with
-- the owning task/workspace and are scoped again by repositories to the local account.
CREATE TABLE planner_activity_events (
    id TEXT PRIMARY KEY NOT NULL,
    workspace_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    event_type TEXT NOT NULL,
    occurred_at_utc TEXT NOT NULL,
    observed_at_utc TEXT,
    origin TEXT NOT NULL,
    description TEXT NOT NULL,
    metadata_json TEXT NOT NULL DEFAULT '{}',
    external_ref_type TEXT,
    external_ref_id TEXT,
    dedupe_key TEXT,
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE,
    UNIQUE(workspace_id, task_id, dedupe_key)
);
CREATE INDEX idx_planner_activity_task_time
    ON planner_activity_events(workspace_id, task_id, occurred_at_utc DESC, id DESC);

CREATE TABLE planner_task_git_commits (
    workspace_id TEXT NOT NULL,
    task_id TEXT NOT NULL,
    commit_sha TEXT NOT NULL,
    association_source TEXT NOT NULL CHECK(association_source IN ('manual','branch','message')),
    branch_name TEXT NOT NULL DEFAULT '',
    subject TEXT NOT NULL DEFAULT '',
    author TEXT NOT NULL DEFAULT '',
    authored_at_utc TEXT,
    committed_at_utc TEXT,
    observed_at_utc TEXT NOT NULL,
    availability TEXT NOT NULL DEFAULT 'reachable' CHECK(availability IN ('reachable','unavailable')),
    PRIMARY KEY(workspace_id, task_id, commit_sha),
    FOREIGN KEY(workspace_id, task_id) REFERENCES planner_tasks(workspace_id, id) ON DELETE CASCADE
);
CREATE INDEX idx_planner_task_git_commits_sha ON planner_task_git_commits(workspace_id, commit_sha);

CREATE TRIGGER planner_activity_task_created AFTER INSERT ON planner_tasks
WHEN NEW.source_type = 'local'
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json,dedupe_key)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'task.created',NEW.created_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Task created',json_object('title',NEW.title),'task.created');
END;

CREATE TRIGGER planner_activity_task_status AFTER UPDATE OF status ON planner_tasks
WHEN OLD.status IS NOT NEW.status
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'task.status',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Status changed',
        json_object('from',OLD.status,'to',NEW.status));
END;

CREATE TRIGGER planner_activity_task_completed AFTER UPDATE OF is_completed ON planner_tasks
WHEN OLD.is_completed IS NOT NEW.is_completed
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'task.completion',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner',
        CASE NEW.is_completed WHEN 1 THEN 'Task completed' ELSE 'Task reopened' END,
        json_object('completed',NEW.is_completed));
END;

CREATE TRIGGER planner_activity_task_title AFTER UPDATE OF title ON planner_tasks
WHEN OLD.title IS NOT NEW.title
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'task.updated',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Title changed',
        json_object('field','title','from',OLD.title,'to',NEW.title));
END;

CREATE TRIGGER planner_activity_task_text AFTER UPDATE OF description, planner_notes ON planner_tasks
WHEN OLD.description IS NOT NEW.description OR OLD.planner_notes IS NOT NEW.planner_notes
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'task.updated',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Task text changed',
        json_object('descriptionChanged',OLD.description IS NOT NEW.description,
                    'notesChanged',OLD.planner_notes IS NOT NEW.planner_notes,
                    'descriptionLength',length(COALESCE(NEW.description,'')),
                    'notesLength',length(COALESCE(NEW.planner_notes,''))));
END;

CREATE TRIGGER planner_activity_task_priority AFTER UPDATE OF priority ON planner_tasks
WHEN OLD.priority IS NOT NEW.priority
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'task.priority',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Priority changed',
        json_object('from',OLD.priority,'to',NEW.priority));
END;

CREATE TRIGGER planner_activity_task_due AFTER UPDATE OF due_date ON planner_tasks
WHEN OLD.due_date IS NOT NEW.due_date
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'task.due_date',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Due date changed',
        json_object('from',OLD.due_date,'to',NEW.due_date));
END;

CREATE TRIGGER planner_activity_task_milestone AFTER UPDATE OF milestone_id ON planner_tasks
WHEN OLD.milestone_id IS NOT NEW.milestone_id
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'task.milestone',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Milestone association changed',
        json_object('from',OLD.milestone_id,'to',NEW.milestone_id));
END;

CREATE TRIGGER planner_activity_task_branch AFTER UPDATE OF git_branch ON planner_tasks
WHEN OLD.git_branch IS NOT NEW.git_branch
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'git.branch',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Git branch association changed',
        json_object('from',OLD.git_branch,'to',NEW.git_branch));
END;

CREATE TRIGGER planner_activity_task_pr AFTER UPDATE OF github_pull_request_url ON planner_tasks
WHEN OLD.github_pull_request_url IS NOT NEW.github_pull_request_url
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json,external_ref_type,external_ref_id)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.id,'pr.reference',NEW.updated_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Pull Request reference changed',
        json_object('from',OLD.github_pull_request_url,'to',NEW.github_pull_request_url),
        'url',NEW.github_pull_request_url);
END;

CREATE TRIGGER planner_activity_note_linked AFTER INSERT ON planner_task_note_links
BEGIN
    INSERT INTO planner_activity_events(id,workspace_id,task_id,event_type,occurred_at_utc,
        observed_at_utc,origin,description,metadata_json,external_ref_type,external_ref_id)
    VALUES(lower(hex(randomblob(16))),NEW.workspace_id,NEW.task_id,'note.linked',NEW.created_at_utc,
        strftime('%Y-%m-%dT%H:%M:%fZ','now'),'planner','Note linked',
        json_object('noteId',NEW.note_id),'note',NEW.note_id);
END;

