# Third-party license inventory

This inventory covers dependencies and tools currently selected by the master plan and Phase 0. Verify the exact license notices shipped with the version actually used before distribution.

| Component | Purpose | License / distribution note |
|---|---|---|
| Qt 6.x (exact build in the package inventory) | Runtime framework | Qt open-source modules are offered under LGPLv3/GPLv3 terms or a commercial license. Use only modules compatible with the project’s selected distribution and meet the applicable notices and relinking obligations. |
| libsodium | Password hashing and future cryptographic primitives | ISC license; include its license notice when distributed. |
| SQLite via Qt QSQLITE | Database driver | SQLite is public domain; bundled driver may also contain Qt licensing obligations. |
| SQLite native library via vcpkg | Interruptible SQL execution and row reads | SQLite is public domain; include the package notice in the dependency inventory. |
| CMake | Build tooling | BSD 3-Clause; developer/CI tool, not a runtime dependency. |
| Ninja | Build tooling | Apache 2.0; developer/CI tool, not a runtime dependency. |
| clang-format / clang-tidy | Formatting and analysis tools | Apache 2.0 with LLVM Exceptions; developer/CI tools, not runtime dependencies. |
| Git | User-installed integration | GPL-2.0-or-later; invoked as an external user tool and not bundled by DevHub. |
| Inno Setup | Planned installer builder | Inno Setup license terms; build-time only. Recheck terms before distributing installer output. |
| DevHub outline icons | Small navigation and action symbols | Project-authored SVG path data released under CC0 1.0 Universal; see `IconLoader::licenseNotice()`. |

Qt licensing is the main distribution constraint. Confirm the exact Qt modules, linkage model, notices, and source-offer obligations for each release build before publishing binaries.
