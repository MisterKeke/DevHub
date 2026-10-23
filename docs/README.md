# DevHub project baseline

`DEVHUB_MASTER_DEVELOPMENT_PLAN.md` at the repository root is the current source of truth for product scope, architecture, and implementation order. Record intentional changes to its binding decisions in `docs/decisions/` before implementing them.

See [ports-and-logs.md](architecture/ports-and-logs.md) for the current Windows Port Monitor and Logs implementation boundaries.

See [sqlite-browser.md](architecture/sqlite-browser.md) for SQLite references, browsing, SQL query safety, and visual edit mode.

See [notes-and-files.md](architecture/notes-and-files.md) for Notes storage/recovery and the workspace File Browser safety boundaries.

See [assistant.md](architecture/assistant.md) for the Assistant Phase 0 wire fixtures,
candidate compatibility table, capability probe evidence and remaining release gates.
The [capability verification report](architecture/assistant-capability-verification.md)
records the follow-up native controls, behavioral results and excluded profiles.
See [assistant-transport.md](architecture/assistant-transport.md) for Phase 1's
native process prototype, bounded JSONL transport, lifecycle tests and ownership limits.

See [workspace-planner.md](architecture/workspace-planner.md) for the embedded Overview
Planner architecture and [workspace-planner-database.md](architecture/workspace-planner-database.md)
for schema version 12, migrations 0009–0012, import ownership, and scale-profile commands.

## Toolchain pins

- Qt 6.11 API baseline, with a local minimum of Qt 6.11.0 and CI pinned to Qt 6.11.2. The 6.11 line is outside the affected range for the Qt Markdown importer issue CVE-2025-3512; keep CI aligned with reviewed Qt advisories.
- Visual Studio 2022 17.14 with the MSVC v143 14.44 toolset family (CI pins compiler package 14.44.35207).
- CMake 3.28 or newer and Ninja Multi-Config for local development.
- vcpkg registry baseline `72adbfe37c8d10cc4497baa358469ab0908482a1`; it supplies native SQLite 3.53.4, which includes the upstream fix for CVE-2026-11822/CVE-2026-11824.

Verify the exact Qt runtime modules and licensing notices for each package before redistribution.

## Dependency updates

Change dependency versions deliberately. Update the vcpkg baseline to a reviewed upstream commit, update the manifest as needed, review the dependency’s license and security notices, and run configure, build, and test presets. Record nontrivial dependency or architecture changes as an ADR. Do not update dependencies implicitly in CI.
