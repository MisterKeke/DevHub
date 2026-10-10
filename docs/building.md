# Building DevHub

Codex is optional and is not a build/CTest dependency. Assistant setup accepts
the tested native `codex-cli 0.160.1` and `0.162.0-alpha.17.2` Windows executables. Select it with Settings
→ Assistant → Browse/Recheck, or leave the override empty for native `codex.exe`
on PATH. Installation locations are not guessed; `.cmd`, `.bat` and PowerShell
wrappers are unsupported. An invalid override never falls back to PATH.
No API key is required or accepted by DevHub. Explicit ChatGPT/device-code
authorization uses Codex-managed sign-in; native Send still requires the runtime
safety gate. Tests use synthetic protocol fixtures, without real sign-in or
inference. The Qt 6.8.3 Windows CI job also runs chat/settings checks at
100/125/150/200% scaling with `tools/verify-assistant-ui.ps1` and retains snapshots.
See [Assistant setup](architecture/assistant-settings.md) for the boundaries.

## Prerequisites

- Windows 11 x64.
- CMake 3.28 or newer.
- Ninja.
- Visual Studio 2022 C++ build tools with v143 14.44 for the CI preset.
- Qt 6.8.3 or newer, including Qt Svg, for a supported Windows x64 compiler kit, available via `QT_ROOT_DIR` when configuring outside Qt Creator. CI is pinned to Qt 6.8.3 for MSVC 2022 x64.
- vcpkg checkout matching the baseline in `vcpkg.json`; set `VCPKG_ROOT` and pass its toolchain file when configuring.
- clang-format and clang-tidy for formatting and static analysis.

## Configure and build

```powershell
$env:QT_ROOT_DIR = 'C:\Qt\6.11.0\mingw_64'
$env:VCPKG_ROOT = 'C:\src\vcpkg'
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
cmake --preset windows-dev -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build --preset windows-dev-debug
ctest --preset windows-dev-debug
```

Use the compiler paired with the selected Qt kit. The `windows-dev` preset uses the static-library `x64-mingw-static` vcpkg triplet so libsodium is linked into DevHub. For an MSVC Qt kit, use the `windows-ci` preset and its `x64-windows` triplet instead.

For a Qt Creator MinGW kit, use the same triplet when configuring:

```powershell
cmake -S . -B out/build/qtcreator-mingw -G Ninja `
  -DCMAKE_PREFIX_PATH='C:/Qt/6.11.0/mingw_64' `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static
```

Set the same `CMAKE_TOOLCHAIN_FILE` and `VCPKG_TARGET_TRIPLET` in Qt Creator's CMake configuration. The manifest installs libsodium for the selected triplet.

When Qt Creator supplies an absolute `CMAKE_CXX_COMPILER` for a MinGW kit,
DevHub puts that compiler's directory first on the configure process's `PATH`
before vcpkg installs dependencies. This keeps dependencies on the same MinGW
compiler/runtime as the application even when MSYS2 is also installed.
If linking reports `libsodium.a ... undefined reference to memset_explicit`,
run **Build → Run CMake** and rebuild. This error can occur when libsodium was
built with a newer MSYS2 compiler while DevHub uses Qt's MinGW compiler;
reconfiguration rebuilds the dependencies for the selected kit.

Build configurations are Debug, RelWithDebInfo, and Release. The CI preset turns warnings into errors and pins the MSVC v143 14.44 toolset.

## Formatting and analysis

Run `tools/format-check.ps1` to check C++ formatting with clang-format. Configure `windows-analysis`, then run clang-tidy over the generated compilation database, for example:

```powershell
cmake --preset windows-analysis -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
clang-tidy -p out/build/windows-analysis src/app/main.cpp tests/smoke_test.cpp
```

`DEVHUB_ENABLE_SANITIZERS` enables AddressSanitizer and UndefinedBehaviorSanitizer for supported non-MSVC toolchains. MSVC sanitizer builds are not configured.

## Windows release package

Install Inno Setup 6 and set `QT_ROOT_DIR` to the Qt kit used by the `windows-package` preset. The packaging script chooses its paired MinGW compiler (or accept `-CompilerBin` when multiple MinGW toolchains are installed). Then run:

```powershell
tools/package-windows.ps1
```

The script builds Release, stages the Qt runtime and QSQLITE plugin, validates SQLite and both QML entry views, adds vcpkg and Qt notices plus a dependency inventory, creates a portable ZIP and a per-user installer, and writes SHA-256 checksums. Use `tools/package-windows.ps1 -PortableOnly` when Inno Setup is unavailable. The installer is unsigned by default. See [packaging/README.md](../packaging/README.md) for signing hooks and release gates. Do not distribute until Qt license obligations and clean-machine checks have been reviewed.

CTest includes failure tests for notes recovery, profile reset rollback, export collisions, background-worker shutdown, and QML imports. Windows CI also installs the Release runtime into an isolated staging folder and checks it with development Qt environment variables cleared. It verifies that removing either a QML plugin or QSQLITE causes failure with useful diagnostics, then restores the plugin. To repeat the positive check manually:

```powershell
tools/check-packaged-runtime.ps1 -Executable out/portable-stage/bin/devhub.exe `
  -ReportPath out/runtime-check.txt
```

The `--check-runtime` executable mode opens only an in-memory SQLite database and instantiates authentication (sign-in and account creation) and the main shell with populated presentation models. It exits nonzero for failed roots or QML warnings; `--check-runtime-report=<path>` records diagnostics. It does not authenticate or open the local profile. This smoke check complements interactive testing on a clean Windows machine.
