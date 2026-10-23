# DevHub

DevHub is a local-first Windows desktop workspace manager for developers. The product scope and implementation authority are in [DEVHUB_MASTER_DEVELOPMENT_PLAN.md](DEVHUB_MASTER_DEVELOPMENT_PLAN.md).

The application currently includes workspace management, an embedded Workspace Planner above the Overview cards, Git and service pages, a Windows TCP/UDP Port Monitor, a bounded file/service Logs viewer, a SQLite browser with backup-gated visual edits, workspace Markdown notes, and a guarded workspace File Browser. Planner tasks are DevHub-owned workspace data. SomethingQt imports are independent copies; there is no synchronization or source write-back. See [Workspace Planner architecture](docs/architecture/workspace-planner.md), its [database and import contract](docs/architecture/workspace-planner-database.md), and [build commands](docs/building.md). Other module boundaries are documented in [ports-and-logs](docs/architecture/ports-and-logs.md), [SQLite Browser](docs/architecture/sqlite-browser.md), [Notes and File Browser](docs/architecture/notes-and-files.md), [core services](docs/architecture/core.md), and [architecture decisions](docs/decisions/).

The project distribution license has not been selected yet. Qt’s open-source license obligations must be reviewed against that choice before binary distribution.

Assistant now replaces Quick Ask inside DevHub. Its streaming UI, controller and
safe Markdown rendering have offline regression coverage. Native Send requires
an executable-specific protocol match and verified runtime restrictions, plus
account, workspace and model checks. See [Codex compatibility](docs/architecture/codex-compatibility.md).
Authenticated inference and clean-machine release validation are
separate checks. See the
[Assistant UI report](docs/architecture/assistant-ui.md) for current behavior and
remaining release checks. Opening the page checks the configured native
`codex.exe` (or PATH when the override is empty); it does not sign in or open an external chat page.
Settings → Assistant provides Browse/Recheck, runtime/account status,
ChatGPT or device-code authorization and optional model choices. No API key is
required. Authorization opens a browser only through its explicit button and
may change Codex's shared sign-in. See [Assistant setup](docs/architecture/assistant-settings.md).

## Running a portable build

Create a portable ZIP with `tools/package-windows.ps1 -PortableOnly`. Extract the entire ZIP into one folder and run `bin/devhub.exe` from that folder. A desktop shortcut may point to this executable, but moving the executable alone will leave behind the Qt DLLs and plugins it needs. The `out/build/windows-dev/Debug/devhub.exe` file is a development build, not a portable package. See [packaging/README.md](packaging/README.md) for release checks.

## Viewing Git history

Select a workspace that contains a Git repository and open **Git**. The page refreshes when opened or when you switch workspaces; **Refresh** is available for changes made while the page stays open. The **History** tab lists the latest 50 commits reachable from all refs. Select a commit to see its diff, and click **Load older commits** to continue through earlier history in batches of 50.
