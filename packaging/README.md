# Windows packaging

The supported packaging path is Windows x64 with a Release build, the matching Qt kit and MinGW compiler, and vcpkg. Run `tools/package-windows.ps1 -PortableOnly` to create `out/release/DevHub-Portable-<version>-x64.zip` without Inno Setup. Run `tools/package-windows.ps1` with Inno Setup 6 installed to create both the portable ZIP and installer. The script stages Qt, QSQLITE, dependency notices, and SHA-256 checksums.

Set `QT_ROOT_DIR` to the Qt kit used by the preset, or pass `-QtRoot`. The script selects the only `Tools/mingw*/bin` compiler it finds; when several are installed, pass `-CompilerBin` for the compiler paired with that Qt kit. Set `VCPKG_ROOT` when using a vcpkg checkout other than `.tools/vcpkg`. `-InnoCompiler` can point to `ISCC.exe` when it is not on `PATH`.

Use `-StageOnly` to build and validate `out/release-stage` (including Qt plugins and license notices) without creating an archive or installer. `-PortableOnly` stages separately in `out/portable-stage`, allowing the ZIP to be rebuilt while an older staged app is running. The package checks an in-memory QSQLITE query and instantiates both QML entry views, including sign-in, account creation, workspace delegates, and overview cards. It clears development Qt/QML environment variables and restricts `PATH` to Windows directories, without opening the local profile. Missing imports, QML warnings, or failed roots stop packaging; diagnostics are written to `out/package-runtime-check.txt`. Windows CI performs the same check on a freshly installed Release runtime.

The `bin/devhub.exe` file requires its adjacent DLLs and the sibling `plugins` and `qml` folders. For a desktop shortcut, point it to `bin/devhub.exe` inside an extracted portable folder; do not copy that executable alone. The `windows-dev` Debug executable is a build artifact and is not deployed for sharing.

Signing is optional for private builds. To sign, pass both `-SignToolPath` and `-CertificateThumbprint`; a timestamp URL can be supplied with `-TimestampServer`. The signing certificate stays in the Windows certificate store and is never copied into the repository or installer.

The installer is per-user and does not require elevation. Normal uninstall preserves Local AppData. The uninstaller asks separately before deleting local DevHub data. Inno Setup closes a running DevHub executable during upgrade.

Do not distribute a portable ZIP or installer until the Qt license choice and notices have been reviewed, and the clean-machine, install, upgrade, uninstall, and release smoke checks in Phase 24 have passed. This repository does not create a release tag or publish artifacts automatically.
