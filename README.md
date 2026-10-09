# DevHub

DevHub is a local-first Windows desktop workspace manager for developers. The product scope and implementation authority are in [DEVHUB_MASTER_DEVELOPMENT_PLAN.md](DEVHUB_MASTER_DEVELOPMENT_PLAN.md).

The application currently includes workspace management, Git and service pages, a Windows TCP/UDP Port Monitor, a bounded file/service Logs viewer, a SQLite browser with backup-gated visual edits, workspace Markdown notes, and a guarded workspace File Browser. See [docs/building.md](docs/building.md) for build commands, [docs/architecture/ports-and-logs.md](docs/architecture/ports-and-logs.md) for the Port Monitor and Logs modules, [docs/architecture/sqlite-browser.md](docs/architecture/sqlite-browser.md) for database behavior and limits, [docs/architecture/notes-and-files.md](docs/architecture/notes-and-files.md) for Notes and File Browser behavior, [docs/architecture/core.md](docs/architecture/core.md) for shared service boundaries, and [docs/decisions/](docs/decisions/) for accepted architecture decisions.

The project distribution license has not been selected yet. Qt’s open-source license obligations must be reviewed against that choice before binary distribution.

Assistant now replaces Quick Ask inside DevHub. Its streaming UI, controller and
safe Markdown rendering are tested offline; native Send remains disabled until
the authenticated Codex runtime/model profile is verified. See the
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
