param(
    [ValidateSet('help', 'portable', 'test')]
    [string]$Task = 'help'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

if ($Task -eq 'help') {
    Write-Host @'
DevHub Windows build shortcuts (use mingw32-make if make is not installed):
  make portable                             Build the default versioned portable ZIP
  make portable ZIP_NAME=DevHub-Retro.zip     Choose the ZIP filename in out/release
  make portable "ZIP_NAME=DevHub Retro.zip"   Filenames with spaces are supported
  make test                                 Configure, build, and run the Debug tests
  make test TEST_FILTER=planner              Run matching CTest targets after building

Options (environment variables or NAME=value arguments):
  QT_ROOT_DIR            Qt MinGW kit; defaults to an existing CMake configuration
  COMPILER_BIN           Matching MinGW bin directory; inferred from cache/Qt Tools
  CMAKE_PATH             cmake.exe; inferred from PATH, cache, or Qt Tools
  JOBS                   Build/test concurrency, 1-64 (default: 4)
  VCPKG_ROOT             vcpkg checkout (default: .tools/vcpkg)
  VCPKG_INSTALLED_DIR    Installed dependencies; defaults to cache or .tools/vcpkg_installed
  INSTALL_DEPS           ON/OFF; defaults to cached policy, or ON for a fresh build

Fresh checkout example:
  make portable QT_ROOT_DIR=D:/Qt/6.11.0/mingw_64
'@
    exit 0
}

function Get-CacheValue([string]$Cache, [string]$Key) {
    if (!(Test-Path -LiteralPath $Cache)) { return '' }
    $match = Select-String -LiteralPath $Cache -Pattern ('^' + [regex]::Escape($Key) + ':[^=]+=(.*)$') |
        Select-Object -First 1
    if ($match) { return $match.Matches[0].Groups[1].Value }
    return ''
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}

$preset = if ($Task -eq 'portable') { 'windows-package' } else { 'windows-dev' }
$cache = Join-Path $repoRoot "out/build/$preset/CMakeCache.txt"
$otherPreset = if ($Task -eq 'portable') { 'windows-dev' } else { 'windows-package' }
$caches = @($cache, (Join-Path $repoRoot "out/build/$otherPreset/CMakeCache.txt"))
$qtRoot = $env:QT_ROOT_DIR
if ([string]::IsNullOrWhiteSpace($qtRoot)) {
    foreach ($candidate in $caches) {
        $qtRoot = (Get-CacheValue $candidate 'CMAKE_PREFIX_PATH').Split(';')[0]
        if ($qtRoot -and (Test-Path -LiteralPath $qtRoot -PathType Container)) { break }
    }
}
if (!$qtRoot -or !(Test-Path -LiteralPath $qtRoot -PathType Container)) {
    throw 'Set QT_ROOT_DIR to your Qt MinGW kit, for example make test QT_ROOT_DIR=D:/Qt/6.11.0/mingw_64.'
}
$qtRoot = (Resolve-Path -LiteralPath $qtRoot).Path
$qtBase = Split-Path (Split-Path $qtRoot -Parent) -Parent
$compilerBin = $env:COMPILER_BIN
if ([string]::IsNullOrWhiteSpace($compilerBin)) {
    foreach ($candidate in $caches) {
        $cachedQt = (Get-CacheValue $candidate 'CMAKE_PREFIX_PATH').Split(';')[0]
        $cachedCompiler = Get-CacheValue $candidate 'CMAKE_CXX_COMPILER'
        if ($cachedQt -and $cachedCompiler -and (Test-Path -LiteralPath $cachedQt) -and
            (Test-Path -LiteralPath $cachedCompiler) -and
            (Resolve-Path -LiteralPath $cachedQt).Path -eq $qtRoot) {
            $compilerBin = Split-Path $cachedCompiler -Parent
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($compilerBin)) {
    $compilers = @(Get-ChildItem -LiteralPath (Join-Path $qtBase 'Tools') -Directory -Filter 'mingw*' |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin/g++.exe') })
    if ($compilers.Count -ne 1) { throw 'Set COMPILER_BIN to the MinGW bin directory paired with this Qt kit.' }
    $compilerBin = Join-Path $compilers[0].FullName 'bin'
}
$compilerBin = (Resolve-Path -LiteralPath $compilerBin).Path
foreach ($tool in @('gcc.exe', 'g++.exe', 'windres.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $compilerBin $tool))) { throw "Missing compiler tool: $compilerBin/$tool" }
}
$cmake = $env:CMAKE_PATH
if ([string]::IsNullOrWhiteSpace($cmake)) {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $cmake = if ($command) { $command.Source } else { Get-CacheValue $cache 'CMAKE_COMMAND' }
    if (!$cmake) { $cmake = Join-Path $qtBase 'Tools/CMake_64/bin/cmake.exe' }
}
$cmake = (Resolve-Path -LiteralPath $cmake).Path
$jobs = 4
if ($env:JOBS -and (![int]::TryParse($env:JOBS, [ref]$jobs) -or $jobs -lt 1 -or $jobs -gt 64)) {
    throw 'JOBS must be an integer from 1 to 64.'
}
$installDeps = $env:INSTALL_DEPS
if (!$installDeps) { $installDeps = Get-CacheValue $cache 'VCPKG_MANIFEST_INSTALL' }
if (!$installDeps) { $installDeps = 'ON' }
if ($installDeps -notin @('ON', 'OFF')) { throw 'INSTALL_DEPS must be ON or OFF.' }
$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $repoRoot '.tools/vcpkg' }
$vcpkgRoot = (Resolve-Path -LiteralPath $vcpkgRoot).Path
$installed = $env:VCPKG_INSTALLED_DIR
if (!$installed) { $installed = Get-CacheValue $cache 'VCPKG_INSTALLED_DIR' }
if (!$installed) { $installed = Join-Path $repoRoot '.tools/vcpkg_installed' }
$installed = [IO.Path]::GetFullPath($installed)
$env:QT_ROOT_DIR = $qtRoot
$env:PATH = "$compilerBin;$(Join-Path $qtRoot 'bin');$(Join-Path $qtBase 'Tools/Ninja');$env:PATH"

Push-Location $repoRoot
try {
    $configure = @('--preset', $preset, "-DCMAKE_PREFIX_PATH=$qtRoot",
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake",
        "-DVCPKG_INSTALLED_DIR=$installed", "-DVCPKG_MANIFEST_INSTALL=$installDeps",
        "-DCMAKE_C_COMPILER=$compilerBin/gcc.exe", "-DCMAKE_CXX_COMPILER=$compilerBin/g++.exe",
        "-DCMAKE_RC_COMPILER=$compilerBin/windres.exe")
    Invoke-Checked $cmake $configure
    if ($Task -eq 'portable') {
        $archiveArguments = @{}
        if ($env:ZIP_NAME) { $archiveArguments.ZipName = $env:ZIP_NAME }
        & (Join-Path $PSScriptRoot 'package-windows.ps1') -PortableOnly @archiveArguments `
            -QtRoot $qtRoot -CompilerBin $compilerBin -CMakePath $cmake -VcpkgRoot $vcpkgRoot `
            -VcpkgInstalledDirectory $installed -BuildParallelism $jobs
    } else {
        Invoke-Checked $cmake @('--build', '--preset', 'windows-dev-debug', '--parallel', "$jobs")
        # Copied development DLLs must still resolve QML imports/plugins from the selected kit.
        $env:QML_IMPORT_PATH = "$(Join-Path $qtRoot 'qml');$env:QML_IMPORT_PATH"
        $env:QT_PLUGIN_PATH = Join-Path $qtRoot 'plugins'
        $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
        $testArguments = @('--preset', 'windows-dev-debug', '--parallel', "$jobs", '--output-on-failure')
        if ($env:TEST_FILTER) { $testArguments += @('--tests-regex', $env:TEST_FILTER) }
        Invoke-Checked $ctest $testArguments
    }
} finally {
    Pop-Location
}
