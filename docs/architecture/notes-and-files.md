# Notes and Workspace Files

## Notes

Notes metadata is scoped to the signed-in account's workspace. Markdown content is stored as UTF-8 under DevHub's local application data directory, using account ID, workspace ID, and NoteId for the path. A note title is metadata and does not rename its backing file. Saves use `QSaveFile` and update a SHA-256 content hash; an external change detected during save prevents DevHub from overwriting the newer file.

The Notes page supports folders, notes, rename, move, tags, pinning, recoverable trash, explicit permanent deletion, Markdown editing and preview, title/tag/content search, and note/folder export. Autosave is debounced. A recovery copy is written under the local application data directory while edits are pending; it can be restored, viewed, or discarded when the note is opened. Pending saves are flushed when changing workspaces, locking, or shutting down. Note content is not encrypted by DevHub.

The Markdown preview does not follow links or load network resources. The Overview shows a notes-and-folders count and preview titles from the Notes page. Local image preview, checkbox editing, line numbers, a dedicated NotesTreeModel, and Notes-specific CommandRegistry actions remain future work. Search reads note content in the background and skips individual files larger than 2 MiB.

## Workspace Files

The Files page roots `QFileSystemModel` at the active workspace and relies on its lazy directory loading. Hidden files are hidden by default and the preference is stored in application settings. Expansion state is restored for at most 100 folders. The text preview is read-only, limited to 1 MiB, and accepts UTF-8/ASCII (including a UTF-8 BOM); binary or unsupported-encoding files are identified without being displayed.

File actions include system-associated open, open-containing-folder, one configured editor executable, absolute and workspace-relative path copying, create file/directory, rename, and confirmed move to the Windows Recycle Bin. Recycle Bin and external editor launch are behind application interfaces. Editor arguments are passed directly as a path argument without shell interpolation. Windows filenames and collisions are checked. The workspace root cannot be renamed or deleted. Resolved paths outside the workspace, including external symbolic links and junctions, are blocked for preview, launch, and mutation.

The configured editor is currently a single saved executable path rather than a multi-profile editor manager. File type icons use QFileSystemModel's cache. Recent-file metadata, CommandRegistry actions, and filesystem boundary tests remain follow-up work. Permanent file deletion is not offered.
