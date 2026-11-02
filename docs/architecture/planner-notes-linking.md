# Planner and Notes linking

Planner tasks and Markdown notes remain owned by their existing repositories. The
`planner_task_note_links` table stores `workspace_id`, `task_id`, `note_id`, a
fixed `note_kind` discriminator, and a creation timestamp. Its composite primary
key prevents duplicate links, and composite foreign keys enforce that both
records belong to the same workspace and that the note-side ID resolves to a
Notes note rather than a folder.
Deleting a task or permanently deleting a note removes its link rows through
cascades. Moving a note to Trash keeps the rows so restoration reveals them again.

`TaskNoteLinkService` is the application boundary for link operations. It checks
that the workspace is active and account-owned, validates task ownership through
`PlannerService`, and obtains note metadata through `INotesRepository`. Titles,
folder paths, tags, and trash state are always read from Notes; Markdown content
is never copied into link metadata. The SQL repository is bound to the local
account and filters archived workspaces from link reads and writes.

Planner task details use the existing Planner controller and QML panel. The note
picker queries note metadata only, limits results, matches titles, folder names,
and tags, and excludes trashed notes. `Create & Link Note` asks the Notes page to
create a note through its existing repository flow; linking happens after
creation succeeds.

Opening a linked note is routed through `MainWindow` to `NotesPage::openNoteById`.
The application checks the signed-in session and active workspace, the Planner
controller checks the current task and relationship, and Notes resolves the
stable note ID through its existing tree and editor path. That path keeps the
existing autosave, dirty-state, external-change, and recovery behavior. A failed
save keeps the current note selected and leaves the editor content in place.

The Notes page shows linked task titles, status, and priority. Opening a task
returns through the existing Planner task-details request. Unlinking either from
Planner or Notes removes only the relationship and refreshes both views.

The link repository is constructed with the active local account ID. UI actions
also require a signed-in session and a selected, non-archived workspace. Archive
and restore leave relationship rows intact while repository operations are
blocked for archived workspaces.
