# DevHub project baseline

`DEVHUB_MASTER_DEVELOPMENT_PLAN.md` at the repository root is the current source of truth for product scope, architecture, and implementation order. Record intentional changes to its binding decisions in `docs/decisions/` before implementing them.

See [ports-and-logs.md](architecture/ports-and-logs.md) for the current Windows Port Monitor and Logs implementation boundaries.

See [sqlite-browser.md](architecture/sqlite-browser.md) for SQLite references, browsing, SQL query safety, and visual edit mode.

See [notes-and-files.md](architecture/notes-and-files.md) for Notes storage/recovery and the workspace File Browser safety boundaries.

## Toolchain pins

- Qt 6.8 API baseline, with CI pinned to Qt 6.8.3 (the Qt 6.8 patch release available to open-source users). Local development accepts Qt 6.8.3 or newer.
- Visual Studio 2022 17.14 with the MSVC v143 14.44 toolset family (CI pins compiler package 14.44.35207).
- CMake 3.28 or newer and Ninja Multi-Config for local development.
- vcpkg registry baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d` (2026-07-29 release).

Qt 6.8.4 and later 6.8 maintenance updates are commercial LTS releases; keep the selected Qt pin compatible with the project’s open-source license policy. Reassess this pin if Qt publishes a newer 6.8 release to the open-source download area.

## Dependency updates

Change dependency versions deliberately. Update the vcpkg baseline to a reviewed upstream commit, update the manifest as needed, review the dependency’s license and security notices, and run configure, build, and test presets. Record nontrivial dependency or architecture changes as an ADR. Do not update dependencies implicitly in CI.
